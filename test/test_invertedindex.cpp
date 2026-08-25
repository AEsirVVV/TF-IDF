// test_invertedindex.cpp —— InvertedIndex 模块的独立演示/测试程序（带 main()）。
//
// 用法：在 VS Code 中打开本文件，按 F6 即可单独编译运行
//       （与 test/test_tokenizer.cpp 同理，单编译单元技巧）：
//         #include "../src/Tokenizer.cpp"
//         #include "../src/InvertedIndex.cpp"
//
// 演示内容：
//   1. 用 Tokenizer 对 data/data1.txt ~ data6.txt 分词；
//   2. 把 6 篇文档加入倒排索引（文档 ID = 0..5）；
//   3. 展示词典大小、每篇文档的词数、若干词条的倒排表；
//   4. 验证词频 tf、文档频率 df、候选集 getCandidates。

#include "InvertedIndex.h"
#include "Tokenizer.h"

#include "../src/InvertedIndex.cpp"
#include "../src/Tokenizer.cpp"

#include <cstdlib>     // std::system（Windows 下切换控制台为 UTF-8）
#include <fstream>     // std::ifstream
#include <iostream>    // std::cout
#include <sstream>     // std::ostringstream

namespace {

// 读取文件全部内容；打不开返回 false。
bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}

// 尝试读取 data/dataN.txt（工作目录不固定，两个候选路径都试一下）。
bool readDataDoc(int n, std::string& out) {
    std::ostringstream p1, p2;
    p1 << "data/data" << n << ".txt";
    p2 << "TF-IDF/data/data" << n << ".txt";
    return readFile(p1.str(), out) || readFile(p2.str(), out);
}

void printInts(const char* label, const std::vector<int>& v) {
    std::cout << label << " [" ;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << v[i];
    }
    std::cout << "]\n";
}

} // namespace

int main() {
#ifdef _WIN32
    std::system("chcp 65001 >nul");   // 控制台切到 UTF-8，避免中文乱码
#endif

    std::cout << "===== InvertedIndex 演示 =====" << "\n\n";

    // ---------- 1. 分词并建索引 ----------
    Tokenizer tok;
    tok.loadStopwords("stopwords.txt");
    if (tok.isStopword("the") == false) {
        tok.loadStopwords("TF-IDF/stopwords.txt");   // 兜底路径
    }

    InvertedIndex index;
    for (int docId = 0; docId < 19; ++docId) {
        std::string text;
        if (!readDataDoc(docId + 1, text)) {
            std::cout << "读取 data/data" << docId + 1 << ".txt 失败（请确认工作目录）\n";
            continue;
        }
        std::vector<std::string> tokens = tok.tokenize(text);
        index.addDocument(docId, tokens);
        std::cout << "docId=" << docId << "  data" << docId + 1
                  << ".txt  词条数=" << tokens.size() << "\n";
    }

    std::cout << "\n词典大小（不同词条总数）: " << index.vocabularySize() << "\n\n";

    // ---------- 2. 每篇文档的总词数 ----------
    std::cout << "----- 每篇文档总词数（getDocWordCount）-----\n";
    for (int docId = 0; docId < 19; ++docId) {
        std::cout << "docId=" << docId << "  ->  " << index.getDocWordCount(docId) << "\n";
    }

    // ---------- 3. 抽查若干词条的倒排表 / 文档频率 ----------
    std::cout << "\n----- 倒排表抽查（词条 -> 文档ID列表）-----\n";
    // 注意：中文按单字切分，所以"数据"在索引中是"数"和"据"两个字条；
    //       其中"数/据"本身是停用词，分词时已被过滤（df=0 是正确行为）。
    const char* sampleTerms[] = {"ai", "machine", "learning", "机", "学", "技"};
    for (const char* t : sampleTerms) {
        std::string term(t);
        std::vector<std::string> q{term};               // 单词查询
        auto postings = index.getCandidates(q);         // 看包含它的文档
        std::cout << "词条 [" << term << "]  df=" << index.getDocumentFrequency(term)
                  << "  出现在文档: ";
        printInts("", postings);
    }

    // ---------- 4. 词频 tf ----------
    std::cout << "\n----- 词频抽查（getTermFrequency）-----\n";
    std::cout << "tf(\"ai\", docId=0) = " << index.getTermFrequency(0, "ai") << "\n";
    std::cout << "tf(\"ai\", docId=5) = " << index.getTermFrequency(5, "ai") << "\n";
    std::cout << "tf(\"machine\", docId=0) = " << index.getTermFrequency(0, "machine") << "\n";
    std::cout << "tf(\"不存在词\", docId=0) = "
              << index.getTermFrequency(0, "不存在词") << "\n";

    // ---------- 5. 候选集 ----------
    std::cout << "\n----- 候选集（getCandidates）-----\n";
    printInts("查询 \"machine\"       -> ", index.getCandidates({"machine"}));
    printInts("查询 \"ai 技\"         -> ", index.getCandidates({"ai", "技"}));
    printInts("查询 \"机 学\"         -> ", index.getCandidates({"机", "学"}));
    printInts("查询 \"不存在的词\"     -> ", index.getCandidates({"不存在的词"}));
    printInts("查询 空查询           -> ", index.getCandidates({}));

    // ---------- 6. 防御性验证 ----------
    std::cout << "\n----- 防御性验证 -----\n";
    InvertedIndex dup;
    dup.addDocument(0, {"a", "a", "b"});
    dup.addDocument(0, {"x", "x", "x", "x"});   // 重复 docId 应被忽略
    std::cout << "重复 addDocument(0) 后 getDocWordCount(0) = "
              << dup.getDocWordCount(0) << "（应为 3，而非 4）\n";
    std::cout << "df(\"a\") = " << dup.getDocumentFrequency("a")
              << "（应为 1）  tf(\"a\",0) = " << dup.getTermFrequency(0, "a")
              << "（应为 2）\n";

    std::cout << "\n===== 演示结束 =====" << std::endl;
    return 0;
}
