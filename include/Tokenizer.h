#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <string>
#include <unordered_set>
#include <vector>

// Tokenizer：文本预处理器。
// 负责把一篇原始文本切分成后续 TF-IDF 计算使用的“词条（token）”。
//
// 主要功能：
//   1. 英文/数字：按连续字母或数字切分，并统一转为小写；
//   2. 中文：按单个汉字切分（不做复杂分词），同时兼容 UTF-8 和 GBK 编码；
//   3. 停用词：从 stopwords.txt 加载后，在 tokenize 时自动过滤；
//   4. 标点符号、空白字符：直接跳过。
class Tokenizer {
public:
    // 默认构造：不加载停用词表。
    Tokenizer() = default;

    // 停用词表设置，从文件加载停用词表，每行一个词。
    // 构造时直接加载停用词表；加载失败不抛出异常，仅停止词表为空。
    explicit Tokenizer(const std::string& stopwordsFilePath);
    // 成功返回 true；文件不存在或无法打开返回 false。
    bool loadStopwords(const std::string& stopwordsFilePath);
    // 手动添加一个停用词。
    void addStopword(const std::string& word);
    // 清空所有停用词。
    void clearStopwords();
    // 判断某个词条是否是停用词。
    bool isStopword(const std::string& token) const;

    // 对原始文本进行分词。
    // 返回已经去除停用词后的词条列表。
    std::vector<std::string> tokenize(const std::string& text) const;

    // 仅将 ASCII 大写字母转为小写；非 ASCII 字节保持不变（避免破坏中文编码）。
    static std::string toLowerAscii(const std::string& text);

private:
    // 判断 text[pos] 处是否是一个 CJK 汉字。
    // 如果返回 true，len 会被设置为该汉字占用的字节数。
    // utf8 表示当前文本是否为合法的 UTF-8 编码；否则按 GBK 处理。
    static bool isCjkChar(const std::string& text, size_t pos, size_t& len, bool utf8);

    // 判断整个文本是否可以被视为 UTF-8 编码。
    static bool isUtf8Text(const std::string& text);

    // 停用词集合。
    std::unordered_set<std::string> stopwords_;
};

#endif // TOKENIZER_H

