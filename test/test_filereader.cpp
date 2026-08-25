// test_filereader.cpp —— FileReader 模块的独立演示/测试程序（带 main()）。
//
// 用法：在 VS Code 中打开本文件，按 F6 即可单独编译运行（单编译单元技巧）：
//         #include "../src/FileReader.cpp"
//
// 演示内容：
//   1. readAllDocuments("data") 读取整目录 .txt，展示 ID / 文件名 / 内容长度；
//   2. 内容可直接喂给 Tokenizer 分词（验证"读取 → 分词"链路）；
//   3. 边界情况：不存在的目录、不存在的文件、单个文件读取。

#include "FileReader.h"

#include "../src/FileReader.cpp"
#include "../src/Tokenizer.cpp"   // 验证"读取 → 分词"链路

#include <cstdlib>     // std::system（Windows 下切换控制台为 UTF-8）
#include <iostream>    // std::cout

int main() {
#ifdef _WIN32
    std::system("chcp 65001 >nul");   // 控制台切到 UTF-8，避免中文乱码
#endif

    std::cout << "===== FileReader 演示 =====" << "\n\n";

    // ---------- 1. 读取整目录（工作目录不固定，两个候选路径都试一下）----------
    FileReader reader;
    std::vector<Document> docs = reader.readAllDocuments("data");
    if (docs.empty()) {
        docs = reader.readAllDocuments("TF-IDF/data");   // 兜底路径
    }

    std::cout << "读入 " << docs.size() << " 篇文档：\n";
    for (const Document& doc : docs) {
        std::cout << "  id=" << doc.id
                  << "  文件名=" << doc.name
                  << "  内容长度=" << doc.content.size() << " 字节\n";
    }

    // ---------- 2. 验证"读取 → 分词"链路（FileReader 产出 → Tokenizer 消费）----------
    std::cout << "\n----- 读取 -> 分词 链路验证（前 3 篇）-----\n";
    Tokenizer tok;
    tok.loadStopwords("stopwords.txt");
    if (tok.isStopword("the") == false) {
        tok.loadStopwords("TF-IDF/stopwords.txt");   // 兜底路径
    }
    for (size_t i = 0; i < docs.size() && i < 3; ++i) {
        std::vector<std::string> tokens = tok.tokenize(docs[i].content);
        std::cout << docs[i].name << " -> " << tokens.size() << " 个词条";
        if (!tokens.empty()) {
            std::cout << "，前 4 个: ["
                      << tokens[0] << "][" << tokens[1] << "][" << tokens[2]
                      << "][" << tokens[3] << "]";
        }
        std::cout << "\n";
    }

    // ---------- 3. 边界情况 ----------
    std::cout << "\n----- 边界情况 -----\n";
    std::cout << "不存在的目录 readAllDocuments(\"no_such_dir\") 返回 "
              << reader.readAllDocuments("no_such_dir").size() << " 篇（应为 0，不抛异常）\n";

    std::string content;
    bool ok = FileReader::readFile("stopwords.txt", content);
    if (!ok) ok = FileReader::readFile("TF-IDF/stopwords.txt", content);
    std::cout << "readFile(\"stopwords.txt\") 成功=" << ok
              << "，读取字节数=" << content.size() << "\n";
    std::cout << "readFile(\"no_such_file.txt\") 成功="
              << FileReader::readFile("no_such_file.txt", content) << "（应为 0）\n";

    std::cout << "\n===== 演示结束 =====" << std::endl;
    return 0;
}
