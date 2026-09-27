// SearchEngine.cpp —— TF-IDF 计算与检索排名模块的实现文件。
//
// 检索流程总览（search）：
//   1. 查询串经 Tokenizer 分词（小写、去停用词）；
//   2. 查询词条按与文档相同的 TF-IDF 公式向量化（稠密向量）；
//   3. 用 InvertedIndex 预筛选候选文档（倒排索引，避免全量扫描）；
//   4. 对每个候选文档计算查询向量与文档向量的余弦相似度；
//   5. 用 std::priority_queue（小顶堆，固定 K 大小）挑出 Top-K；
//   6. 堆内元素倒序输出 → 相似度从高到低的结果列表。

#include "SearchEngine.h"
#include "ThreadPool.h"   // 线程池：build() 的三个阶段都复用它

#include <algorithm> 
#include <cmath>     
#include <functional> // std::function（把"分片循环体"传给通用并行器）
#include <future>    // std::future（等待并行分片完成）
#include <memory>    // std::unique_ptr（线程池只在并行分支创建）
#include <queue>     
#include <thread>    // std::thread::hardware_concurrency
#include <utility>   

// 构造：初始化内部分词器并加载停用词表。
SearchEngine::SearchEngine(const std::string& stopwordsFilePath)
    : tokenizer_(stopwordsFilePath) {}

// 重新加载停用词表（透传给内部 Tokenizer）。
bool SearchEngine::loadStopwords(const std::string& stopwordsFilePath) {
    return tokenizer_.loadStopwords(stopwordsFilePath);
}

// 构建索引与 TF-IDF 向量集。
//
// 算法（一个线程池贯穿三个阶段）：
//   0. 【并行】按块并行分词，每个线程只写自己的分片 tokenized[i]；
//   1. 【串行】合并共享结构：写倒排索引 + 收集词典（词 -> 维度）；
//   2. 【并行】统计文档频率 df（按词典分块，各线程写自己的局部结果）；
//   3. 【并行】逐文档生成 TF-IDF 稠密向量（各线程只写自己那段 vectors[i]）。
//
// 为什么这样并行（每一处都由实测数据决定）：
//   - 这三步都是"每篇文档/每个词条独立计算"，天然可并行；
//   - 需要写共享结构的两处（倒排索引、词典维度分配）留在单线程阶段，
//     避免"多线程写同一份 map 再加锁"的锁竞争；
//   - 线程池只创建一次、复用三个阶段：线程创建 + join 有固定开销
//     （本机约 1.3ms），开三次等于白付三笔；
//   - tokenizer_.tokenize 与 computeIdf 都是 const/只读，多线程调用安全；
//   - 各线程只写"属于自己的下标"（tokenized[i] / vectors[i] /
//     dfOfTerm[j]），不需要任何锁 —— 这是无锁并行的前提：
//     共享数据只读，私有数据各写各的。
void SearchEngine::build(const std::vector<Document>& docs) {
    // 清空旧状态，使 build 可重复调用（重建索引）
    docs_.clear();
    termToDim_.clear();
    documentFrequency_.clear();
    docVectors_.clear();
    index_.clear();
    totalDocs_ = 0;
    vocabSize_ = 0;
    poolSize_ = 0;

    docs_ = docs;
    totalDocs_ = static_cast<int>(docs.size());
    if (docs.empty()) {
        return;                    // 空语料：没有任何可索引内容
    }

    const size_t docCount = docs.size();

    // ---------- 准备：按阈值决定本次构建是串行还是并行 ----------
    // 并行不是越多越好：线程创建 + join + 调度有固定开销（本机实测约 1.3 ms，
    // 与机器和线程数有关），语料太小时这个开销远大于并行省下的时间。实测：
    //   19 篇   串行 0.22 ms / 并行 1.5 ms  → 0.15x（纯亏）
    //   190 篇  串行 2.0  ms / 并行 1.8 ms  → 约 1.1x（打平）
    //   475 篇  串行 4.5  ms / 并行 2.1 ms  → 约 2.1x
    //   1900 篇 串行 18   ms / 并行 5.6 ms  → 约 3.0x
    // 所以默认阈值 256：明显越过交叉点才并行，避免"为并行而并行"。
    // 本项目语料 500 篇 > 256，因此默认走并行分支（线程池真正被用到）。
    //
    // 线程数不必超过任务数：文档少时开满线程纯属浪费。
    const bool useParallel = (docCount >= parallelThreshold_);
    size_t workers = 0;
    std::unique_ptr<ThreadPool> pool;
    if (useParallel) {
        size_t desired = static_cast<size_t>(std::thread::hardware_concurrency());
        if (desired == 0) {
            desired = 2;                           // 拿不到硬件并发度时的兜底
        }
        workers = std::min(desired, docCount);
        pool.reset(new ThreadPool(workers));
        poolSize_ = workers;                       // 供 threadCount() 观察
    } else {
        poolSize_ = 1;                             // 串行：本次构建只用主线程
    }

    // 把一个区间 [0, count) 按线程数分块，交给线程池并行执行；
    // 串行分支下就退化为"直接在主线程里跑一遍"，两条路径共用同一段循环体，
    // 保证串行/并行只有"谁来跑"的区别，没有"跑什么"的区别。
    auto forEachChunk = [&](size_t count, const std::function<void(size_t, size_t)>& body) {
        if (!pool) {
            body(0, count);
            return;
        }
        if (count == 0) {
            return;
        }
        const size_t chunk = (count + workers - 1) / workers;   // 向上取整分块
        std::vector<std::future<void>> futures;
        futures.reserve(workers);
        for (size_t start = 0; start < count; start += chunk) {
            const size_t end = std::min(start + chunk, count);
            futures.push_back(pool->submit([&body, start, end]() { body(start, end); }));
        }
        for (std::future<void>& f : futures) {
            f.get();          // 等待所有分片完成（任务里的异常也在此抛出）
        }
    };

    // ---------- 第 0 步：分词（并行；每个线程只写 tokenized[i]） ----------
    std::vector<std::vector<std::string>> tokenized(docCount);
    forEachChunk(docCount, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            tokenized[i] = tokenizer_.tokenize(docs[i].content);
        }
    });

    // ---------- 第 1 步：单线程合并——建倒排索引 + 收集词典 ----------
    // 这两件都要写共享结构（index_、termToDim_），保持单线程最省心：
    // 一旦多线程写同一个 map，就必须加锁，而锁竞争会把并行退化成串行。
    for (size_t i = 0; i < docCount; ++i) {
        index_.addDocument(docs[i].id, tokenized[i]);
        for (const std::string& t : tokenized[i]) {
            if (termToDim_.find(t) == termToDim_.end()) {
                // 新词分配一个递增的维度下标
                termToDim_.emplace(t, static_cast<int>(termToDim_.size()));
            }
        }
    }
    vocabSize_ = termToDim_.size();   // 向量维度 = 词典大小

    // ---------- 第 2 步：文档频率 df（并行；各线程写自己的 dfOfTerm[j]） ----------
    // 先把词典摊平成 vector（序号 -> 词条），这样才能按下标分块并行；
    // 各线程只写 dfOfTerm 中属于自己的那一段，最后单线程搬进 map。
    std::vector<std::string> terms;
    terms.reserve(vocabSize_);
    for (const auto& kv : termToDim_) {
        terms.push_back(kv.first);
    }
    std::vector<int> dfOfTerm(vocabSize_, 0);
    forEachChunk(vocabSize_, [&](size_t begin, size_t end) {
        for (size_t j = begin; j < end; ++j) {
            dfOfTerm[j] = index_.getDocumentFrequency(terms[j]);   // 只读倒排索引
        }
    });
    for (size_t j = 0; j < vocabSize_; ++j) {
        documentFrequency_[terms[j]] = dfOfTerm[j];
    }

    // ---------- 第 3 步：逐文档生成 TF-IDF 稠密向量（并行；各写 vectors[i]） ----------
    // 这是整个构建里最重的一步（500 篇时约占一半时间：每篇要建一次词频表
    // 并分配一条 2205 维的稠密向量），所以它最值得并行。
    // 输入（tokenized / termToDim_ / documentFrequency_）全部只读，
    // 输出按文档下标切开，线程之间零共享 → 不需要锁。
    std::vector<std::vector<double>> vectors(docCount);
    forEachChunk(docCount, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const std::vector<std::string>& tokens = tokenized[i];

            // 1) 词频统计（同一词出现几次）
            std::unordered_map<std::string, int> tf;
            for (const std::string& t : tokens) {
                ++tf[t];
            }

            // 2) 逐词填充向量：weight = tf * idf
            //    （词典中绝大多数维度该文档没出现，保持 0，即"稀疏填充稠密向量"）
            std::vector<double> vec(vocabSize_, 0.0);
            for (const auto& kv : tf) {
                const int dim = termToDim_.at(kv.first);           // 词 -> 维度
                const double idf = computeIdf(documentFrequency_.at(kv.first));
                vec[dim] = static_cast<double>(kv.second) * idf;
            }
            vectors[i] = std::move(vec);
        }
    });

    // ---------- 第 4 步：登记到 docVectors_（单线程，只是移动指针，极快） ----------
    for (size_t i = 0; i < docCount; ++i) {
        docVectors_[docs[i].id] = std::move(vectors[i]);
    }
}   // 线程池在此析构：join 全部工作线程（优雅退出）

// 检索主流程。
std::vector<SearchResult> SearchEngine::search(const std::string& query, int topK) const {
    std::vector<SearchResult> results;
    // 空查询 / 非正 topK / 空语料：直接返回空结果
    if (query.empty() || topK <= 0 || vocabSize_ == 0) {
        return results;
    }

    // ---------- 1. 查询串分词（与文档同一套预处理） ----------
    std::vector<std::string> qterms = tokenizer_.tokenize(query);
    if (qterms.empty()) {          // 例如查询全是停用词
        return results;
    }

    // ---------- 2. 查询向量化（与文档向量同一公式） ----------
    std::unordered_map<std::string, int> qtf;
    for (const std::string& t : qterms) {
        ++qtf[t];
    }
    std::vector<double> qvec(vocabSize_, 0.0);
    for (const auto& kv : qtf) {
        auto dfIt = documentFrequency_.find(kv.first);
        if (dfIt == documentFrequency_.end()) {
            continue;              // 词不在词典中：对任何文档权重都是 0
        }
        const int dim = termToDim_.at(kv.first);
        qvec[dim] = static_cast<double>(kv.second) * computeIdf(dfIt->second);
    }

    // ---------- 3. 倒排索引预筛选候选文档 ----------
    std::vector<int> candidates = index_.getCandidates(qterms);

    // 查询词条去重（覆盖度统计需要"不同词条数"作分母；
    // 查询里重复出现的词条不影响覆盖度）。
    std::vector<std::string> distinctTerms;
    for (const std::string& t : qterms) {
        if (std::find(distinctTerms.begin(), distinctTerms.end(), t) ==
            distinctTerms.end()) {
            distinctTerms.push_back(t);
        }
    }

    // ---------- 4. 对候选文档打分，并用堆选出 Top-K ----------
    // 堆元素：<相似度, 文档ID>（保留 ID，输出时才能做"是否包含全部查询词"
    // 的相关判断）。std::greater 使 priority_queue 成为 "小顶堆"，堆始终只
    // 保留当前最好的 topK 个；来了新分数就入堆，堆超过 K 个就弹出最小的。
    //
    // 【查询覆盖度惩罚】得分 = 余弦相似度 × (命中词条数 / 查询词条数)。
    // 原因：检索是 OR 语义（候选 = 含任一查询词的文档），纯余弦相似度只衡量
    // "重叠程度"，会让"只含部分查询词、但该词高频"的文档排到"含全部查询词"
    // 的文档前面（如查"机器学习"，教育文档只有"学/习"却排在 data6 前面）。
    // 乘上覆盖度比例后，全命中的相关文档权重不变，部分命中的噪声文档按比例
    // 降权，排序结果与"相关判断"（含全部词条才算符合）保持一致。
    using HeapNode = std::pair<double, int>;
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> heap;

    const double coverageDenominator = static_cast<double>(distinctTerms.size());

    for (int docId : candidates) {
        double score = computeCosineSimilarity(qvec, docVectors_.at(docId));

        // 统计该文档命中多少个不同的查询词条（候选集保证至少命中 1 个）
        int covered = 0;
        for (const std::string& t : distinctTerms) {
            if (index_.getTermFrequency(docId, t) > 0) {
                ++covered;
            }
        }
        if (coverageDenominator > 0.0) {
            score *= static_cast<double>(covered) / coverageDenominator;
        }

        heap.emplace(score, docId);              // (分数, 文档ID) 入堆
        if (static_cast<int>(heap.size()) > topK) {
            heap.pop();                          // 挤出当前"最差"的一个
        }
    }

    // ---------- 5. 输出：小顶堆全弹出得到升序，反转后为降序（最佳在前） ----------
    // 每条结果附带 relevant：该文档是否包含查询的"全部"词条（相关判断）。
    results.reserve(heap.size());
    while (!heap.empty()) {
        const int docId = heap.top().second;
        const double score = heap.top().first;
        heap.pop();

        SearchResult r;
        r.docName = docNameById(docId);
        r.score = score;
        r.relevant = containsAllTerms(docId, qterms);
        results.push_back(std::move(r));
    }
    std::reverse(results.begin(), results.end());
    return results;
}

// 文档总数 N。
size_t SearchEngine::documentCount() const {
    return docs_.size();
}

// 词典大小（TF-IDF 向量维度）。
size_t SearchEngine::vocabularySize() const {
    return vocabSize_;
}

// 构建时使用的线程池大小（并行分词用）。
size_t SearchEngine::threadCount() const {
    return poolSize_;
}

// 设置并行阈值（文档数达到该值才启用线程池）。
void SearchEngine::setParallelThreshold(size_t threshold) {
    parallelThreshold_ = threshold;
}

// 当前并行阈值。
size_t SearchEngine::parallelThreshold() const {
    return parallelThreshold_;
}

// 两个等长向量的余弦相似度：cos = 点积 / (模长1 × 模长2)。
// 算法：一遍循环同时累加点积与两个模长的平方；最后开方相除。
// 余弦相似度衡量"方向"而非"大小"，对 TF-IDF 权重天然合适：文档越长、词越多，不会因为"量多"而得分高。
// 零向量（没有任何非零权重）的模长为 0，直接返回 0（无特征可比）。
double SearchEngine::computeCosineSimilarity(const std::vector<double>& v1, const std::vector<double>& v2) {
    double dot = 0.0;
    double norm1 = 0.0;
    double norm2 = 0.0;
    for (size_t i = 0; i < v1.size(); ++i) {
        dot += v1[i] * v2[i];
        norm1 += v1[i] * v1[i];
        norm2 += v2[i] * v2[i];
    }
    if (norm1 == 0.0 || norm2 == 0.0) {
        return 0.0;
    }
    return dot / (std::sqrt(norm1) * std::sqrt(norm2));
}

// 经典 IDF 公式：idf = ln(N / df)。
// 含义：df 越大（词越常见）权重越低，df 越小（词越稀有）权重越高——
// 这正是"逆文档频率"的核心思想。df 等于 N 时 idf 为 0（所有文档都有的词没有区分度）；df <= 0 是防御分支（词不在任何文档中，权重为 0）。
double SearchEngine::computeIdf(int df) const {
    if (df <= 0) {
        return 0.0;
    }
    return std::log(static_cast<double>(totalDocs_) / static_cast<double>(df));
}

// 通过文档 ID 找文件名：线性扫描 docs_。
// 文档数少（本项目个位数），线性查找比建索引映射更直白。
std::string SearchEngine::docNameById(int docId) const {
    for (const Document& doc : docs_) {
        if (doc.id == docId) {
            return doc.name;
        }
    }
    return std::string();      // 找不到返回空串（理论上不会发生）
}

// 相关度判断：文档是否包含查询的"全部"词条。
// 用倒排索引查每个词条在该文档的词频（>0 即包含）。
// 注意：查询词条经过去停用词、去重与否都不影响——停用词已被分词器过滤，
// 这里只判断剩余的有效词条是否全部出现在文档中。
bool SearchEngine::containsAllTerms(int docId, const std::vector<std::string>& terms) const {
    for (const std::string& t : terms) {
        if (index_.getTermFrequency(docId, t) <= 0) {
            return false;
        }
    }
    return true;
}
