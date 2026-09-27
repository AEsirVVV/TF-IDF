// test_threadpool.cpp —— 线程池（ThreadPool）与并行构建演示（带 main()）。
//
// 演示内容：
//   1. 基本用法：向线程池提交任务，用 std::future 取回结果；
//   2. 并发执行验证：多个任务确实并行跑完（原子计数器统计完成数）；
//   3. 优雅退出：线程池析构时 join 全部线程，不会挂死或残留线程；
//   4. 项目集成：SearchEngine::build 按语料规模选择串行/并行，
//      并验证并行构建的相似度与串行构建完全一致。

#include "SearchEngine.h"
#include "ThreadPool.h"

#include "../src/InvertedIndex.cpp"
#include "../src/SearchEngine.cpp"
#include "../src/ThreadPool.cpp"
#include "../src/Tokenizer.cpp"

#include <atomic>      // std::atomic（无锁计数）
#include <cstdio>
#include <cstdlib>     // std::system
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

bool readDataDoc(int n, std::string& out) {
    std::ostringstream p1, p2;
    p1 << "data/data" << n << ".txt";
    p2 << "TF-IDF/data/data" << n << ".txt";
    std::ifstream in(p1.str());
    if (!in.is_open()) { in.open(p2.str()); }
    if (!in.is_open()) { return false; }
    std::ostringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}

} // namespace

int main() {
#ifdef _WIN32
    std::system("chcp 65001 >nul");
#endif

    std::cout << "===== ThreadPool 演示 =====" << std::endl;

    // ---------- 0. 硬件并发度 ----------
    std::cout << "CPU 硬件并发度 hardware_concurrency() = "
              << std::thread::hardware_concurrency() << std::endl;

    // ---------- 1. 基本用法：提交任务 + future 取结果 ----------
    {
        ThreadPool pool;   // 线程数 = 硬件并发度
        std::cout << "线程池已创建，工作线程数 = " << pool.size() << std::endl;

        // 提交 8 个任务：各自计算 n*n
        std::vector<std::future<long long>> futures;
        for (int n = 1; n <= 8; ++n) {
            futures.push_back(pool.submit([n]() -> long long {
                return static_cast<long long>(n) * n;
            }));
        }

        std::cout << "任务结果（应依次为 1,4,9,...,64）: ";
        for (std::future<long long>& f : futures) {
            std::cout << f.get() << " ";
        }
        std::cout << std::endl;
    }   // 线程池析构 -> join 全部线程（优雅退出）

    // ---------- 2. 并发执行验证：原子计数器 ----------
    {
        constexpr int kTasks = 64;
        std::atomic<int> counter{0};      // 原子操作：多线程自增不丢计数
        {
            ThreadPool pool;
            for (int i = 0; i < kTasks; ++i) {
                pool.submit([&counter]() {
                    ++counter;            // 原子自增，无需加锁
                });
            }
        }   // 析构会等所有任务跑完
        std::cout << "并发任务完成数 = " << counter.load()
                  << "（应等于 " << kTasks << "）" << std::endl;
    }

    // ---------- 3. 项目集成：并行构建与串行结果完全一致 ----------
    std::vector<Document> docs;
    for (int i = 1; i <= 19; ++i) {
        std::string text;
        if (!readDataDoc(i, text)) { continue; }
        std::ostringstream name;
        name << "data" << i << ".txt";
        docs.push_back({i - 1, name.str(), text});
    }

    // (a) 小语料（19 篇 < 并行阈值 256）：走串行分词，不付线程开销
    SearchEngine small;
    if (!small.loadStopwords("stopwords.txt")) {
        small.loadStopwords("TF-IDF/stopwords.txt");
    }
    small.build(docs);

    std::cout << "\n【小语料】文档数 = " << small.documentCount()
              << "，线程数 = " << small.threadCount()
              << "（应 = 1：低于阈值 256，走串行，避免无谓线程开销）"
              << "，词典大小 = " << small.vocabularySize() << "（应为 1245）" << std::endl;

    auto smallRes = small.search("机器学习", 3);
    std::cout << "  查询 \"机器学习\"：" << std::endl;
    for (size_t i = 0; i < smallRes.size(); ++i) {
        std::printf("    #%zu %-12s %.2f%%  相关=%s\n", i + 1, smallRes[i].docName.c_str(),
                    smallRes[i].score * 100.0, smallRes[i].relevant ? "是" : "否");
    }

    // (b) 大语料（复制 24 份 → 456 篇 ≥ 阈值）：走线程池并行分词
    std::vector<Document> big;
    big.reserve(docs.size() * 24);
    for (int rep = 0; rep < 24; ++rep) {
        for (const Document& d : docs) {
            Document copy = d;
            copy.id = static_cast<int>(big.size());
            copy.name = d.name + "#" + std::to_string(rep);
            big.push_back(copy);
        }
    }

    SearchEngine large;
    if (!large.loadStopwords("stopwords.txt")) {
        large.loadStopwords("TF-IDF/stopwords.txt");
    }
    large.build(big);                    // ← 内部用线程池并行分词

    std::cout << "\n【大语料】文档数 = " << large.documentCount()
              << "，线程数 = " << large.threadCount() << "（应 > 1：并行分支已生效）"
              << "，词典大小 = " << large.vocabularySize()
              << "（应仍为 1245：重复文档不产生新词）" << std::endl;

    auto largeRes = large.search("机器学习", 3);
    std::cout << "  查询 \"机器学习\"：" << std::endl;
    for (size_t i = 0; i < largeRes.size(); ++i) {
        std::printf("    #%zu %-12s %.2f%%  相关=%s\n", i + 1, largeRes[i].docName.c_str(),
                    largeRes[i].score * 100.0, largeRes[i].relevant ? "是" : "否");
    }

    // (c) 一致性断言：复制语料时 N 与 df 同比例放大，idf = ln(N/df) 不变，
    //     因此并行构建出的每篇文档相似度应与 19 篇串行构建完全相同。
    //     注意：24 个副本内容相同 → 分数相同 → 名次并列，所以不能逐位比对，
    //     而要按"文档家族"（dataX.txt vs dataX.txt#rep）对照分数。
    auto largeAll = large.search("机器学习", 1000);   // 取全部文档的分数
    std::unordered_map<std::string, double> scoreOf;
    for (const SearchResult& r : largeAll) {
        scoreOf[r.docName] = r.score;
    }

    bool same = true;
    std::cout << "\n一致性检查（串行 19 篇 vs 并行 456 篇，逐篇对照相似度）：" << std::endl;
    for (const SearchResult& r : smallRes) {
        auto it = scoreOf.find(r.docName + "#0");
        if (it == scoreOf.end()) {
            same = false;
            std::cout << "    " << r.docName << " 在并行结果中缺失 ✗" << std::endl;
            continue;
        }
        const double diff = it->second - r.score;
        if (diff > 1e-12 || diff < -1e-12) { same = false; }
        std::printf("    %-12s 串行 %6.2f%%  并行 %6.2f%%  %s\n", r.docName.c_str(),
                    r.score * 100.0, it->second * 100.0,
                    (diff <= 1e-12 && diff >= -1e-12) ? "一致" : "不一致 ✗");
    }
    std::cout << "结论：并行构建与串行构建 " << (same ? "结果完全一致 ✓（多线程没有引入错误）"
                                                       : "结果不一致 ✗")
              << std::endl;
    std::cout << "（原理：语料整体复制 k 份后 N 与 df 同步放大，idf = ln(N/df) 不变，"
              << "每篇文档的向量和余弦相似度都不变；副本并列同名次）" << std::endl;

    std::cout << "\n===== 演示结束 =====" << std::endl;
    return 0;
}
