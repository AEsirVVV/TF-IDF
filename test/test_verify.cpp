// test_verify.cpp —— 查询结果正确性验证程序（断言式）。
//
// 验证思路（分层，共 57 条断言）：
//   1. 金标准（原始 13 个题材）：对 20 个主题明确的查询，断言"预期文档必须排第一名"；
//   2. 金标准（扩充语料 80 个新题材）：同样断言 top-1，覆盖新加入的语料；
//   3. 全相关不变量：对"同一题材多篇文档"的查询，断言 Top-5 全部 relevant=true
//      （验证覆盖度惩罚确实把噪声文档压下去了）；
//   4. 边界断言：纯停用词 / 空查询 / 不存在的词 / 语料中确实未出现的英文实词
//      → 必须无结果；
//   5. 结构断言：语料规模、词典规模、并行分支生效（线程数 > 1）、
//      build 幂等（重复构建结果不变）；
//   6. 手算对照数据：打印 basketball 的 tf / df / idf，
//      便于按第 2 节的方法手工核算余弦相似度。

#include "FileReader.h"
#include "InvertedIndex.h"
#include "SearchEngine.h"

#include "../src/FileReader.cpp"
#include "../src/InvertedIndex.cpp"
#include "../src/SearchEngine.cpp"
#include "../src/ThreadPool.cpp"   // SearchEngine::build 使用线程池并行
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

// 第 1 类：原始 19 篇语料的主题金标准（13 个题材，中英对照）
const Expect kGoldTests[] = {
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
};

// 第 2 类：扩充语料（data20 ~ data500，80 个新题材）的主题金标准。
// 每个题材有 5~6 篇文档，这里断言"该题材得分最高的那篇"必须排第一名。
const Expect kNewThemeTests[] = {
    // —— 中文新题材 ——
    {"茶文化",           "data100.txt", false},
    {"围棋",             "data344.txt", false},
    {"咖啡",             "data190.txt", false},
    {"鸟类",             "data124.txt", false},
    {"钓鱼",             "data76.txt",  false},
    {"香料",             "data80.txt",  false},
    {"灯笼",             "data254.txt", false},
    {"漆艺",             "data258.txt", false},
    {"早市",             "data242.txt", false},
    {"旧书",             "data486.txt", false},
    {"瑜伽",             "data390.txt", false},
    {"山脉",             "data38.txt",  false},
    // —— 英文新题材 ——
    {"starter",          "data421.txt", false},
    {"espresso",         "data103.txt", false},
    {"barometer",        "data389.txt", false},
    {"quoin",            "data47.txt",  false},
    {"escapement",       "data99.txt",  false},
    {"joinery",          "data347.txt", false},
    {"slate",            "data485.txt", false},
    {"willow",           "data491.txt", false},
};

// 第 3 类：全相关不变量——这些查询的命中文档"全部"是相关文档
// （题材词只在同题材文档里出现），因此 Top-5 必须条条 relevant=true。
// 这直接验证了"覆盖度惩罚 + 相关判断"的一致性：噪声文档排在后面。
const char* kAllRelevantQueries[] = {
    "咖啡", "围棋", "山脉", "昆虫", "瑜伽", "陶瓷",
    "espresso", "sourdough",
};

// 第 4 类：边界断言——必须返回空结果。
const Expect kBoundaryTests[] = {
    {"the",     nullptr, true},   // 纯停用词
    {"",        nullptr, true},   // 空查询
    {"zzzqqq",  nullptr, true},   // 确定不存在的词
    {"apiary",  nullptr, true},   // 语料里确实没出现的英文实词（扩充语料未采到该词）
    {"kayak",   nullptr, true},   // 同上
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
    std::cout << "语料 " << N << " 篇，词典 " << engine.vocabularySize()
              << "，构建线程数 " << engine.threadCount()
              << "（并行阈值 " << engine.parallelThreshold() << "）\n";

    int pass = 0, fail = 0;

    // 通用断言器：跑一条 Expect 并计数
    auto runExpect = [&](const Expect& t) {
        const std::vector<SearchResult> results = engine.search(t.query, 5);

        // 通用不变量：分数范围 + 降序 + 数量上限
        bool invariantOk = true;
        for (size_t i = 0; i < results.size(); ++i) {
            if (results[i].score < 0.0 || results[i].score > 1.0) invariantOk = false;
            if (i > 0 && results[i - 1].score < results[i].score) invariantOk = false;
        }
        if (results.size() > 5) invariantOk = false;

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
    };

    // ---------- 2. 第 1 类：原始题材金标准 ----------
    std::cout << "\n===== 第 1 类：原始语料题材金标准（13 个题材 / 20 条）=====\n";
    for (const Expect& t : kGoldTests) {
        runExpect(t);
    }

    // ---------- 3. 第 2 类：扩充语料新题材金标准 ----------
    std::cout << "\n===== 第 2 类：扩充语料新题材金标准（80 个题材中抽 20 条）=====\n";
    for (const Expect& t : kNewThemeTests) {
        runExpect(t);
    }

    // ---------- 4. 第 3 类：全相关不变量（Top-5 必须全部相关）----------
    std::cout << "\n===== 第 3 类：全相关不变量（Top-5 全部 relevant=true）=====\n";
    for (const char* q : kAllRelevantQueries) {
        const std::vector<SearchResult> results = engine.search(q, 5);
        bool ok = !results.empty();
        for (const SearchResult& r : results) {
            if (!r.relevant) ok = false;
        }
        if (ok) {
            ++pass;
            std::printf("PASS  [%s] -> Top-%zu 全部相关（最高 %.2f%%）\n",
                        q, results.size(), results[0].score * 100.0);
        } else {
            ++fail;
            std::printf("FAIL  [%s] 期望 Top-5 全部相关，实际: ", q);
            for (const SearchResult& r : results) {
                std::printf("%s%s ", r.docName.c_str(), r.relevant ? "(相关)" : "(否)");
            }
            std::printf("\n");
        }
    }

    // ---------- 5. 第 4 类：边界断言 ----------
    std::cout << "\n===== 第 4 类：边界断言（必须无结果）=====\n";
    for (const Expect& t : kBoundaryTests) {
        runExpect(t);
    }

    // ---------- 6. 第 5 类：结构与行为断言 ----------
    std::cout << "\n===== 第 5 类：结构与行为断言 =====\n";
    struct StructCheck {
        const char* name;
        bool ok;
        std::string detail;
    };
    std::vector<StructCheck> structs;

    structs.push_back({"语料规模 N = 500（扩充后）", N == 500, "N = " + std::to_string(N)});
    structs.push_back({"词典规模 = 2205 词", engine.vocabularySize() == 2205,
                       "词典 = " + std::to_string(engine.vocabularySize())});
    structs.push_back({"500 篇 > 阈值 256，并行分支生效", engine.threadCount() > 1,
                       "线程数 = " + std::to_string(engine.threadCount())});

    // build 幂等：重新构建同一批文档，top-1 与分数必须完全一致
    const std::vector<SearchResult> before = engine.search("机器学习", 5);
    engine.build(docs);
    const std::vector<SearchResult> after = engine.search("机器学习", 5);
    bool idem = (before.size() == after.size());
    if (idem) {
        for (size_t i = 0; i < before.size(); ++i) {
            if (before[i].docName != after[i].docName ||
                before[i].score != after[i].score) { idem = false; }
        }
    }
    structs.push_back({"build 幂等（重复构建结果不变）", idem,
                       idem ? "两次构建 top-1 分数逐位相同"
                            : "两次构建结果出现差异"});

    for (const StructCheck& s : structs) {
        if (s.ok) { ++pass; } else { ++fail; }
        std::printf("%s  %s（%s）\n", s.ok ? "PASS " : "FAIL ", s.name, s.detail.c_str());
    }

    // ---------- 7. 手算对照数据（以 basketball 为例）----------
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
    std::cout << "  程序输出 ≈ 45.88%，代入上式可反推文档向量模长 |d3| 验证。\n\n";

    // ---------- 8. 汇总 ----------
    std::cout << "========================================\n";
    std::cout << "通过 " << pass << " 条，失败 " << fail << " 条";
    if (fail == 0) {
        std::cout << "  —— 全部通过，结果正确性得到验证 ✓\n";
    } else {
        std::cout << "  —— 有断言失败，请检查上述 FAIL 项\n";
    }
    return fail == 0 ? 0 : 1;
}
