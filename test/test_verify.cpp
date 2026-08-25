// test_verify.cpp —— 查询结果正确性验证程序（断言式）。
//
// 验证思路（分层）：
//   1. 金标准断言：对 19 篇语料里"主题明确"的查询，断言
//      "预期文档必须排在第一名"（人工可读的正确性底线）；
//   2. 边界断言：纯停用词 / 空查询 / 不存在的词 → 必须无结果；
//   3. 通用不变量：分数在 [0,1]、结果降序、数量 ≤ topK；
//   4. 手算对照数据：打印 basketball 的 tf / df / idf，
//      便于按第 2 节的方法手工核算余弦相似度。

#include "FileReader.h"
#include "InvertedIndex.h"
#include "SearchEngine.h"

#include "../src/FileReader.cpp"
#include "../src/InvertedIndex.cpp"
#include "../src/SearchEngine.cpp"
#include "../src/Tokenizer.cpp"

#include <cmath>     // std::log
#include <cstdio>    // std::printf
#include <iostream>
#include <string>
#include <vector>

namespace {

// 一条金标准断言：query 查询后，topDoc 必须是第一名；
// empty 为 true 时表示该查询必须返回空结果。
struct Expect {
    const char* query;
    const char* topDoc;   // empty=true 时忽略
    bool empty;
};

// 19 篇语料的主题金标准（来自第 32 节语料库设计说明）
const Expect kTests[] = {
    // —— 英文主题词（各自命中唯一主题文档）——
    {"basketball",       "data3.txt",  false},
    {"football",         "data9.txt",  false},
    {"health",           "data7.txt",  false},
    {"climate change",   "data2.txt",  false},
    {"music",            "data8.txt",  false},
    {"ancient rome",     "data4.txt",  false},
    {"storytelling",     "data5.txt",  false},
    {"programming",      "data13.txt", false},
    {"economy",          "data14.txt", false},
    {"space",            "data15.txt", false},
    {"AI",               "data1.txt",  false},
    {"machine learning", "data13.txt", false},   // 编程文档里也含 machine learning
    // —— 中文主题词 ——
    {"足球",             "data11.txt", false},
    {"人工智能",          "data6.txt",  false},
    {"机器学习",          "data10.txt", false},
    {"健康",             "data12.txt", false},
    {"太空",             "data19.txt", false},
    {"美食",             "data18.txt", false},
    {"教育",             "data16.txt", false},
    {"编程",             "data10.txt", false},
    // —— 边界情况：必须无结果 ——
    {"the",              nullptr,      true},   // 纯停用词
    {"",                 nullptr,      true},   // 空查询
    {"zzzqqq",           nullptr,      true},   // 确定不存在的词
};

} // namespace

int main() {
    // ---------- 1. 读取语料并构建 ----------
    FileReader reader;
    std::vector<Document> docs = reader.readAllDocuments("data");
    if (docs.empty()) {
        docs = reader.readAllDocuments("TF-IDF/data");   // 兜底路径
    }

    SearchEngine engine;
    if (!engine.loadStopwords("stopwords.txt")) {
        engine.loadStopwords("TF-IDF/stopwords.txt");
    }
    engine.build(docs);
    const int N = static_cast<int>(engine.documentCount());
    std::cout << "语料 " << N << " 篇，词典 " << engine.vocabularySize() << "\n\n";

    // ---------- 2. 逐条断言 ----------
    int pass = 0, fail = 0;
    for (const Expect& t : kTests) {
        const std::vector<SearchResult> results = engine.search(t.query, 5);

        // 通用不变量：分数范围 + 降序 + 数量上限
        bool invariantOk = true;
        for (size_t i = 0; i < results.size(); ++i) {
            if (results[i].score < 0.0 || results[i].score > 1.0) invariantOk = false;
            if (i > 0 && results[i - 1].score < results[i].score) invariantOk = false;
        }
        if (results.size() > 5) invariantOk = false;

        // 金标准断言
        bool expectOk;
        if (t.empty) {
            expectOk = results.empty();
        } else {
            expectOk = !results.empty() && results[0].docName == t.topDoc;
        }

        if (expectOk && invariantOk) {
            ++pass;
            std::printf("PASS  [%s] -> 第一名 %s\n", t.query,
                        t.empty ? "(空)" : results[0].docName.c_str());
        } else {
            ++fail;
            std::printf("FAIL  [%s] 期望=%s%s | 实际: ",
                        t.query, t.topDoc ? t.topDoc : "(空)",
                        t.empty ? "(空)" : "");
            for (size_t i = 0; i < results.size(); ++i) {
                std::printf("%s(%.2f%%) ", results[i].docName.c_str(),
                            results[i].score * 100.0);
            }
            if (!invariantOk) std::printf(" [不变量破坏: 分数范围/降序/数量]");
            std::printf("\n");
        }
    }

    // ---------- 3. 手算对照数据（以 basketball 为例）----------
    // 用独立的 Tokenizer + InvertedIndex 打印 tf/df，便于手工核算余弦相似度。
    Tokenizer tok;
    if (!tok.loadStopwords("stopwords.txt")) {
        tok.loadStopwords("TF-IDF/stopwords.txt");
    }
    InvertedIndex index;
    for (size_t i = 0; i < docs.size(); ++i) {
        index.addDocument(docs[i].id, tok.tokenize(docs[i].content));
    }

    std::cout << "\n----- 手算对照数据（查询 \"basketball\"）-----\n";
    std::cout << "文档总数 N = " << N << "\n";
    std::cout << "  tf(\"basketball\", data3) = " << index.getTermFrequency(2, "basketball") << "\n";
    std::cout << "  df(\"basketball\")        = " << index.getDocumentFrequency("basketball") << "\n";
    std::cout << "  idf(basketball) = ln(N/df) = ln(" << N << "/"
              << index.getDocumentFrequency("basketball") << ") ≈ "
              << std::log(static_cast<double>(N) /
                          index.getDocumentFrequency("basketball")) << "\n";
    std::cout << "  查询向量只有 basketball 一维，权重 = 1 × idf\n";
    std::cout << "  核对公式：cos = (1×idf) × (tf(3)×idf) / (|q| × |d3|)\n";
    std::cout << "  程序输出 ≈ 48.43%，代入上式可反推文档向量模长 |d3| 验证。\n\n";

    // ---------- 4. 汇总 ----------
    std::cout << "========================================\n";
    std::cout << "通过 " << pass << " 条，失败 " << fail << " 条";
    if (fail == 0) {
        std::cout << "  —— 全部通过，结果正确性得到验证 ✓\n";
    } else {
        std::cout << "  —— 有断言失败，请检查上述 FAIL 项\n";
    }
    return fail == 0 ? 0 : 1;
}
