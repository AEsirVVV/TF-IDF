// FileReader.cpp —— 文件读取模块的实现文件。
//
// 核心流程（readAllDocuments）：
//   1. 打开目录；
//   2. 收集所有 .txt 文件名；
//   3. 按文件名排序（保证文档 ID 顺序确定）；
//   4. 逐个读取内容，依次分配 ID 0, 1, 2, ...，产出 Document 列表。

#include "FileReader.h"

#include <algorithm>    
#include <cctype>       
#include <filesystem>   // std::filesystem（C++17 标准库目录遍历）
#include <fstream>      // std::ifstream
#include <sstream>      // std::ostringstream

namespace fs = std::filesystem;

namespace {

// 判断文件名是否为 .txt（不区分大小写，data1.TXT 也视为文本文件）。
bool isTxtFile(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".txt";
}

// "自然序"比较两个文件名："data2.txt" < "data10.txt"。
// 普通字典序会把 data10 排在 data2 前面（逐字符比较 '1' < '2'），
// 与直觉的"数字大小"顺序不符；这里把连续数字当成一个整体按数值比较，
// 保证文档 ID 按 data1, data2, ..., data10, ... 的自然顺序分配。
bool naturalLess(const fs::path& a, const fs::path& b) {
    const std::string& sa = a.filename().string();
    const std::string& sb = b.filename().string();
    size_t i = 0, j = 0;
    while (i < sa.size() && j < sb.size()) {
        const bool da = std::isdigit(static_cast<unsigned char>(sa[i]));
        const bool db = std::isdigit(static_cast<unsigned char>(sb[j]));
        if (da && db) {
            // 都遇到数字：取整段连续数字，先比长度（位数少的小），再逐位比较
            size_t i2 = i, j2 = j;
            while (i2 < sa.size() && std::isdigit(static_cast<unsigned char>(sa[i2]))) ++i2;
            while (j2 < sb.size() && std::isdigit(static_cast<unsigned char>(sb[j2]))) ++j2;
            const size_t lenA = i2 - i, lenB = j2 - j;
            if (lenA != lenB) return lenA < lenB;
            const int cmp = sa.compare(i, lenA, sb, j, lenB);
            if (cmp != 0) return cmp < 0;
            i = i2; j = j2;          // 数字段相等，继续比较后续字符
        } else if (sa[i] != sb[j]) {
            return sa[i] < sb[j];    // 普通字符按字典序
        } else {
            ++i; ++j;
        }
    }
    return sa.size() < sb.size();    // 前缀相同，短的排前面
}

} 

// 读取目录下所有 .txt 文件。
std::vector<Document> FileReader::readAllDocuments(const std::string& dirPath) const {
    std::vector<Document> docs;

    // 1. 打开目录。
    //  传入 std::error_code 的重载不会抛异常：目录不存在时 ec 被置位，iterator 变为"末尾迭代器"，我们据此返回空列表。
    std::error_code ec;
    fs::directory_iterator it(dirPath, ec);
    if (ec) {
        return docs;                 // 目录不存在 / 无权限 → 空列表
    }

    // 2. 收集目录下所有 .txt 文件的路径。
    //    directory_iterator 的遍历顺序不保证，先收集、后排序，避免"同一目录每次读出的文档顺序不同"。
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : it) {
        if (entry.is_regular_file(ec) && isTxtFile(entry.path())) {
            files.push_back(entry.path());
        }
    }

    // 3. 按文件名"自然序"排序（data2 < data10），保证文档 ID 与文件名的
    //    对应关系稳定且符合直觉。
    std::sort(files.begin(), files.end(), naturalLess);

    // 4. 逐个读取并分配 ID（0, 1, 2, ...）。
    for (const fs::path& file : files) {
        std::string content;
        if (!readFile(file.string(), content)) {
            continue;                // 单个文件失败：跳过，不影响其他文件
        }
        Document doc;
        doc.id = static_cast<int>(docs.size());   // 已成功读取的个数即下一个 ID
        doc.name = file.filename().string();
        doc.content = std::move(content);         // 移动，避免大字符串拷贝
        docs.push_back(std::move(doc));
    }

    return docs;
}

// 读取单个文件的全部内容。
bool FileReader::readFile(const std::string& filePath, std::string& out) {
    // 二进制模式读取：不做换行符转换、不做编码转换，
    // 保证 UTF-8 / GBK 等任意字节内容原样进入字符串，
    // 编码识别与分词统一交给 Tokenizer 处理。
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) {
        return false;                // 文件不存在或无法打开
    }

    std::ostringstream buf;
    buf << in.rdbuf();               // 把整个文件流一次性灌进缓冲区
    out = buf.str();
    return true;
}
