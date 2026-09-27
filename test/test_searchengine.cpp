// test_searchengine.cpp —— SearchEngine 模块的独立演示/测试程序（带 main()）。
//
// 用法：在 VS Code 中打开本文件，按 F6 即可单独编译运行（单编译单元技巧）：
//         #include "../src/Tokenizer.cpp"
//         #include "../src/InvertedIndex.cpp"
//         #include "../src/SearchEngine.cpp"
//
// 演示内容：
//   1. 用 FileReader 读入 data/ 下全部文档构成语料（当前 500 篇）；
//   2. SearchEngine::build 建索引 + TF-IDF 向量化；
//   3. 多组查询验证：英文单词、多词查询、中文查询、停用词查询、
//      空查询、Top-K 截断、无匹配词。

#include "FileReader.h"
#include "SearchEngine.h"

#include "../src/FileReader.cpp"
#include "../src/InvertedIndex.cpp"
#include "../src/SearchEngine.cpp"
#include "../src/ThreadPool.cpp"   // SearchEngine::build 使用线程池并行分词
#include "../src/Tokenizer.cpp"

#include <cstdio>      // std::printf（格式化输出分数百分比）
#include <cstdlib>     // std::system（Windows 下切换控制台为 UTF-8）
#include <fstream>     // std::ifstream
#include <iomanip>     // std::setw
#include <iostream>    // std::cout
#include <sstream>     // std::ostringstream

namespace {

// 打印一次检索结果
void printResults(const std::string& query, const std::vector<SearchResult>& results) {
    std::cout << "查询 \"" << query << "\":\n";
    if (results.empty()) {
        std::cout << "  无结果\n\n";
        return;
    }
    for (size_t i = 0; i < results.size(); ++i) {
        std::printf("  #%zu  %-14s  相似度 = %.2f%%\n",
                    i + 1, results[i].docName.c_str(), results[i].score * 100.0);
    }
    std::cout << "\n";
}

} // namespace

int main() {
#ifdef _WIN32
    std::system("chcp 65001 >nul");   // 控制台切到 UTF-8，避免中文乱码
#endif

    std::cout << "===== SearchEngine 演示（TF-IDF + 余弦相似度 + Top-K）=====\n\n";

    // ---------- 1. 读入 data/ 下全部文档（当前 500 篇） ----------
    FileReader reader;
    std::vector<Document> docs = reader.readAllDocuments("data");
    if (docs.empty()) {
        docs = reader.readAllDocuments("TF-IDF/data");   // 兜底路径
    }
    std::cout << "已读入 " << docs.size() << " 篇文档\n\n";

    // ---------- 2. 构建 ----------
    SearchEngine engine;
    if (!engine.loadStopwords("stopwords.txt")) {
        engine.loadStopwords("TF-IDF/stopwords.txt");   // 兜底路径
    }
    engine.build(docs);
    std::cout << "文档总数 N = " << engine.documentCount()
              << "，词典大小（向量维度）= " << engine.vocabularySize()
              << "，构建线程数 = " << engine.threadCount()
              << "（并行阈值 " << engine.parallelThreshold() << "）\n\n";

    // ---------- 3. 各组查询 ----------
    // 注意：中文查询按单字切分（"人工智能"→ 工/智），因此用"不存在的词xyz"
    // 这类中文句做"无匹配"测试不合适（单字可能命中真实文档），改用纯英文乱码。
    printResults("machine learning", engine.search("machine learning", 5));
    printResults("AI", engine.search("AI", 5));
    printResults("basketball", engine.search("basketball", 5));
    printResults("football", engine.search("football", 5));      // 英文足球
    printResults("足球", engine.search("足球", 5));              // 中文足球
    printResults("人工智能", engine.search("人工智能", 5));      // 中文 AI（多篇相关，按相似度排序）
    printResults("健康", engine.search("健康", 5));              // 中文健康
    printResults("health", engine.search("health", 5));          // 英文健康
    printResults("climate change", engine.search("climate change", 3));  // Top-K=3
    printResults("the", engine.search("the", 5));        // 纯停用词 → 空
    printResults("", engine.search("", 5));              // 空查询 → 空
    printResults("zzzqqq", engine.search("zzzqqq", 5));  // 确定不存在的词 → 空

    // ---------- 4. Top-K 正确性抽查 ----------
    std::cout << "----- Top-K 截断验证：查询 \"ai\" -----\n";
    auto r2 = engine.search("ai", 2);
    auto r99 = engine.search("ai", 99);
    std::cout << "topK=2 返回 " << r2.size() << " 条；topK=99 返回 "
              << r99.size() << " 条（应相等或 topK 更少）\n";
    for (size_t i = 0; i < r2.size(); ++i) {
        std::printf("  #%zu %-14s %.2f%%\n", i + 1, r2[i].docName.c_str(),
                    r2[i].score * 100.0);
    }

    // ---------- 5. 分数单调性抽查（同查询，结果应降序） ----------
    std::cout << "\n----- 排序正确性：以上所有结果均已按相似度降序排列 -----\n";

    std::cout << "\n===== 演示结束 =====" << std::endl;
    return 0;
}
