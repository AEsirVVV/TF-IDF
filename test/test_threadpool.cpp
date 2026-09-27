// test_threadpool.cpp —— 线程池（ThreadPool）与并行构建演示（带 main()）。
//
// 演示内容：
//   1. 基本用法：向线程池提交任务，用 std::future 取回结果；
//   2. 并发执行验证：多个任务确实并行跑完（原子计数器统计完成数）；
//   3. 优雅退出：线程池析构时 join 全部线程，不会挂死或残留线程；
//   4. 项目集成：用真实语料（500 篇）验证
//      —— 100 篇 < 阈值 256 走串行（线程数 = 1）；
//      —— 500 篇 > 阈值 256 走并行（线程数 = 32）；
//      —— 同一份 500 篇语料，用 setParallelThreshold 强制串行做对照组，
//         逐名次比对分数，证明多线程没有引入任何错误。

#include "FileReader.h"
#include "SearchEngine.h"
#include "ThreadPool.h"

#include "../src/FileReader.cpp"
#include "../src/InvertedIndex.cpp"
#include "../src/SearchEngine.cpp"
#include "../src/ThreadPool.cpp"
#include "../src/Tokenizer.cpp"

#include <algorithm>   // std::min
#include <atomic>      // std::atomic（无锁计数）
#include <cstdio>
#include <cstdlib>     // std::system
#include <iostream>
#include <limits>      // std::numeric_limits（强制串行用）
#include <vector>

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

    // ---------- 3. 项目集成：同一份语料下，并行构建与串行构建必须完全一致 ----------
    // 真实语料（当前 500 篇，> 默认阈值 256），用 FileReader 读入，与实际服务同源。
    FileReader reader;
    std::vector<Document> docs = reader.readAllDocuments("data");
    if (docs.empty()) {
        docs = reader.readAllDocuments("TF-IDF/data");
    }
    if (docs.empty()) {
        std::cout << "\n读不到 data/ 语料，跳过集成演示（请确认工作目录）" << std::endl;
        return 0;
    }

    // (a) 小语料（取前 100 篇 < 阈值 256）：应走串行分支，不付线程开销
    std::vector<Document> smallDocs(docs.begin(),
                                    docs.begin() + std::min<size_t>(100, docs.size()));
    SearchEngine small;
    if (!small.loadStopwords("stopwords.txt")) {
        small.loadStopwords("TF-IDF/stopwords.txt");
    }
    small.build(smallDocs);
    std::cout << "\n【小语料】文档数 = " << small.documentCount()
              << "，线程数 = " << small.threadCount()
              << "（应 = 1：低于阈值 " << small.parallelThreshold() << "，走串行，不付线程开销）"
              << std::endl;

    // (b) 全量语料（500 篇 > 阈值 256）：默认走线程池并行分词
    SearchEngine parallel;
    if (!parallel.loadStopwords("stopwords.txt")) {
        parallel.loadStopwords("TF-IDF/stopwords.txt");
    }
    parallel.build(docs);
    std::cout << "【全量语料】文档数 = " << parallel.documentCount()
              << "，线程数 = " << parallel.threadCount() << "（应 > 1：并行分支已生效）"
              << "，词典大小 = " << parallel.vocabularySize() << std::endl;

    // (c) 同一份 500 篇语料，把阈值调到 SIZE_MAX 强制串行 —— 得到对照组。
    //     这样"串行 vs 并行"是在完全相同的输入上比较，最能说明多线程没写坏数据。
    SearchEngine serial;
    if (!serial.loadStopwords("stopwords.txt")) {
        serial.loadStopwords("TF-IDF/stopwords.txt");
    }
    serial.setParallelThreshold(std::numeric_limits<size_t>::max());   // 强制串行
    serial.build(docs);
    std::cout << "【强制串行】文档数 = " << serial.documentCount()
              << "，线程数 = " << serial.threadCount() << "（应 = 1：阈值 = SIZE_MAX）"
              << "，词典大小 = " << serial.vocabularySize() << std::endl;

    // (d) 逐篇对照：对多个查询取全部文档的分数，逐名次比较（分数与顺序都要一致）
    const char* kQueries[] = {"机器学习", "basketball", "茶文化", "starter", "铁路"};
    bool same = true;
    std::cout << "\n一致性检查（同一份 " << docs.size()
              << " 篇语料：并行 vs 强制串行，逐名次对照）：" << std::endl;
    for (const char* q : kQueries) {
        const std::vector<SearchResult> rp = parallel.search(q, 100000);   // 取全部命中
        const std::vector<SearchResult> rs = serial.search(q, 100000);

        bool ok = (rp.size() == rs.size());
        size_t mismatch = 0;
        if (ok) {
            for (size_t i = 0; i < rp.size(); ++i) {
                const double diff = rp[i].score - rs[i].score;
                if (rp[i].docName != rs[i].docName || diff > 1e-12 || diff < -1e-12) {
                    ok = false;
                    ++mismatch;
                }
            }
        }
        if (!ok) { same = false; }
        std::printf("    查询 %-12s 命中 %5zu 篇  名次与分数 %s",
                    q, rp.size(), ok ? "完全一致 ✓\n" : "存在差异 ✗\n");
        if (!ok && rp.size() > 0) {
            std::printf("      （不一致名次数 = %zu，例如并行 #1 = %s / 串行 #1 = %s）\n",
                        mismatch, rp[0].docName.c_str(), rs[0].docName.c_str());
        }
    }
    std::cout << "结论：并行构建与串行构建 " << (same ? "结果完全一致 ✓（多线程没有引入错误）"
                                                        : "结果不一致 ✗")
              << std::endl;

    // (e) 抽查：默认路径（500 篇并行）的检索结果仍是熟悉的排序
    auto res = parallel.search("机器学习", 3);
    std::cout << "\n【检索抽查】查询 \"机器学习\"（并行构建的索引）：" << std::endl;
    for (size_t i = 0; i < res.size(); ++i) {
        std::printf("    #%zu %-12s %.2f%%  相关=%s\n", i + 1, res[i].docName.c_str(),
                    res[i].score * 100.0, res[i].relevant ? "是" : "否");
    }

    std::cout << "\n===== 演示结束 =====" << std::endl;
    return 0;
}
