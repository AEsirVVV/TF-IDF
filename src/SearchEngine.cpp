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
#include "ThreadPool.h"   // 线程池：build() 并行分词

#include <algorithm> 
#include <cmath>     
#include <future>    // std::future（等待并行分片完成）
#include <queue>     
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
// 算法（并行分词 + 两遍合并）：
//   0. 【多线程】把文档按块分给线程池并行分词，每个线程只写自己的局部结果
//      （tokenized[i]），互不加锁 —— 这就是"无锁并行"的做法；
//   1. 主线程单线程合并：写倒排索引 + 收集词典（词 -> 维度）；
//   2. 文档频率 df；
//   3. 逐文档生成 TF-IDF 稠密向量（复用第 0 步已分好的词条，避免重复分词）。
//
// 为什么这样并行：
//   - 分词是纯计算、文档之间完全独立，是天然可并行点（CPU 密集）；
//   - 共享结构（倒排索引、词典）的写入放在单线程合并阶段，
//     避免"多线程写同一份 map 再加锁"的锁竞争；
//   - tokenizer_.tokenize 是 const 方法、只读停用词表，多线程同时调用安全。
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

    // ---------- 第 0 步：分词（小语料串行 / 大语料并行，按阈值选择） ----------
    // 并行不是越多越好：线程创建 + join + 调度有固定开销（本机实测约 1.3 ms，
    // 与机器和线程数有关），语料太小时这个开销远大于分词本身。实测数据：
    //   19 篇   串行 0.22 ms / 并行 1.5 ms  → 0.15x（纯亏）
    //   190 篇  串行 2.0  ms / 并行 1.8 ms  → 约 1.1x（打平）
    //   475 篇  串行 4.5  ms / 并行 2.1 ms  → 约 2.1x
    //   1900 篇 串行 18   ms / 并行 5.6 ms  → 约 3.0x
    // 所以取 kParallelThreshold = 256：明显越过交叉点才并行，避免"为并行而并行"。
    std::vector<std::vector<std::string>> tokenized(docCount);
    constexpr size_t kParallelThreshold = 256;
    if (docCount < kParallelThreshold) {
        // 小语料：串行分词（避免无谓的线程开销）
        for (size_t i = 0; i < docCount; ++i) {
            tokenized[i] = tokenizer_.tokenize(docs[i].content);
        }
        poolSize_ = 1;                     // 本次构建实际使用的线程数（串行）
    } else {
        // 大语料：线程池并行分词，每个线程写自己的分片，无锁
        // 线程数不必超过任务数：文档少时开满线程纯属浪费。
        size_t desired = static_cast<size_t>(std::thread::hardware_concurrency());
        if (desired == 0) {
            desired = 2;                       // 拿不到硬件并发度时的兜底
        }
        const size_t threadCount = std::min(desired, docCount);

        ThreadPool pool(threadCount);
        poolSize_ = pool.size();
        const size_t chunk = (docCount + poolSize_ - 1) / poolSize_;   // 向上取整分块

        std::vector<std::future<void>> futures;
        futures.reserve(poolSize_);
        for (size_t start = 0; start < docCount; start += chunk) {
            const size_t end = std::min(start + chunk, docCount);
            futures.push_back(pool.submit([this, &docs, &tokenized, start, end]() {
                for (size_t i = start; i < end; ++i) {
                    // 只写 tokenized[i]（本线程独占的分片），不碰任何共享结构
                    tokenized[i] = tokenizer_.tokenize(docs[i].content);
                }
            }));
        }
        for (std::future<void>& f : futures) {
            f.get();                           // 等待所有分片完成（异常也会在此抛出）
        }
    }   // 线程池析构：join 全部工作线程（优雅退出）

    // ---------- 第 1 步：单线程合并——建倒排索引 + 收集词典 ----------
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

    // ---------- 文档频率 df：每个词出现在几篇文档中 ----------
    for (const auto& kv : termToDim_) {
        documentFrequency_[kv.first] = index_.getDocumentFrequency(kv.first);
    }

    // ---------- 第 2 步：逐文档生成 TF-IDF 稠密向量（复用已分词结果） ----------
    for (size_t i = 0; i < docCount; ++i) {
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
        docVectors_[docs[i].id] = std::move(vec);
    }
}

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
