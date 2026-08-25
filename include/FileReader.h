#ifndef FILEREADER_H
#define FILEREADER_H

#include <string>
#include <vector>

// Document：语料中的一篇文档。
struct Document {
    int id;                 // 文档唯一 ID
    std::string name;       // 文档文件名，如 "data1.txt"
    std::string content;    // 原始文本内容
};

// FileReader：文件读取模块。
//
// 职责：
//   读取指定目录（如 data/）下的所有 .txt 文件，
//   为每篇文档自动分配唯一 ID 并记录文件名，产出 Document 列表，
//   直接作为 SearchEngine::build 的输入。
//
class FileReader {
public:
    FileReader() = default;

    // 读取目录 dirPath 下所有 .txt 文件，返回 Document 列表。
    // 行为约定：
    //   - 文件名按字典序排序后依次分配 ID 0, 1, 2, ...（保证 ID 顺序确定）；
    //   - 非 .txt 文件与子目录一律跳过（扩展名比较不区分大小写）；
    //   - 目录不存在或为空 → 返回空列表（不抛异常）；
    //   - 某个文件读取失败 → 跳过该文件，不影响其他文件。
    std::vector<Document> readAllDocuments(const std::string& dirPath) const;

    // 读取单个文件的全部内容到 out。
    // 以二进制模式读取，字节原样保留（不转换换行、不做编码猜测）。
    // 成功返回 true；文件不存在或无法打开返回 false。
    static bool readFile(const std::string& filePath, std::string& out);
};

#endif // FILEREADER_H
