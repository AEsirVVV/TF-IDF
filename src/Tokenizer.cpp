//Tokenizer.cpp —— 文本预处理（分词）模块的实现文件。
//
//核心流程（tokenize）：
//  1. 先用启发式方法判断文本编码（UTF-8 或 GBK）；
//  2. 从左到右扫描文本：
//       - 遇到 CJK 汉字 -> 按单个汉字切出一个词条；
//       - 遇到 ASCII 字母或数字 -> 切出连续的"字母/数字串"作为一个词条；
//       - 其余字符（空格、标点等）-> 直接跳过；
//  3. 所有词条统一转小写（只对 ASCII 大写字母生效，不影响中文）；
//  4. 最后过滤掉停用词（stopwords）。

#include "Tokenizer.h"
#include <cctype>    // std::isalnum
#include <fstream>   // std::ifstream（读取停用词表文件）

namespace {
//把一行文本左右两端的空白字符（空格、制表符等）去掉。
//只处理 ASCII 空白，不破坏中文字节。
std::string trimAsciiWhitespace(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();

    while (begin < end) {
        char c = s[begin];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            ++begin;
        } else {
            break;
        }
    }
    while (end > begin) {
        char c = s[end - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            --end;
        } else {
            break;
        }
    }

    return s.substr(begin, end - begin);
    }   
} 

// 构造时直接加载停用词表。
// 若文件不存在或无法打开，只是停用词表为空，不抛异常（保持构造安全）。
Tokenizer::Tokenizer(const std::string& stopwordsFilePath) {
    loadStopwords(stopwordsFilePath);
}

// 从文件加载停用词表：每行一个词。
// 处理细节：
//   - 兼容 Windows 的 CRLF 换行（行尾多余的 '\r' 会被修剪掉）；
//   - 空行与纯空白行跳过；
//   - 停用词统一转小写后存入集合，这样"THE"、"The"、"the"
//     都能匹配到小写后的词条 "the"；
//   - 重复加载时，旧的停用词表会被清空（以新文件为准）。
bool Tokenizer::loadStopwords(const std::string& stopwordsFilePath) {
    std::ifstream in(stopwordsFilePath);   // RAII：析构时自动关闭文件
    if (!in.is_open()) {
        return false;                      // 打开失败，调用方自行处理
    }

    stopwords_.clear();                    // 用新表整体替换旧表

    std::string line;
    while (std::getline(in, line)) {       // 逐行读取
        line = trimAsciiWhitespace(line);  // 去掉行首行尾空白（含 '\r'）
        if (line.empty()) {
            continue;                      // 跳过空行
        }
        stopwords_.insert(toLowerAscii(line)); // 小写归一化后加入集合
    }

    return true;
}

// 手动添加一个停用词（同样做小写归一化，与 tokenize 的输出保持一致）。
void Tokenizer::addStopword(const std::string& word) {
    stopwords_.insert(toLowerAscii(word));
}

// 清空所有停用词。
void Tokenizer::clearStopwords() {
    stopwords_.clear();
}

// 判断某个词条是否为停用词。
// 注意：调用方传入的词条应已是小写形式（tokenize 的输出天然满足）。
bool Tokenizer::isStopword(const std::string& token) const {
    return stopwords_.find(token) != stopwords_.end();
}

// 核心接口：对原始文本分词，返回过滤停用词后的词条列表。
//
// 算法：
//   1. 空文本直接返回空列表；
//   2. 用 isUtf8Text() 判断文本是UTF-8还是GBK；
//   3. 用下标 i 从左到右扫描：
//        a. isCjkChar(text, i, len, utf8) 为真 → 取出一个汉字
//           （UTF-8 占 3 字节 / GBK 占 2 字节），小写后作为词条；
//        b. 否则如果当前字节是 ASCII 字母或数字 → 向后吃掉连续的
//           字母/数字，得到"单词或数字串"，小写后作为词条；
//        c. 否则（空格、标点、其他符号）→ i 前进 1 个字节跳过；
//   4. 最后遍历一遍，剔除停用词。
//
// 时间复杂度：O(n)，n 为文本字节数（每个字节至多被访问常数次）。
std::vector<std::string> Tokenizer::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    if (text.empty()) {
        return tokens;
    }

    // 只判断一次编码，整段文本统一处理
    const bool utf8 = isUtf8Text(text);

    const size_t n = text.size();
    size_t i = 0;

    while (i < n) {
        // (a) 汉字：按"单字切分"
        size_t cjkLen = 0;
        if (isCjkChar(text, i, cjkLen, utf8)) {
            tokens.push_back(toLowerAscii(text.substr(i, cjkLen)));
            i += cjkLen;                 // 跳过整个汉字
            continue;
        }

        // (b) ASCII 字母或数字：切出连续的一段
        if (std::isalnum(static_cast<unsigned char>(text[i]))) {
            const size_t start = i;
            while (i < n && std::isalnum(static_cast<unsigned char>(text[i]))) {
                ++i;                     // 吃掉连续的字母/数字
            }
            tokens.push_back(toLowerAscii(text.substr(start, i - start)));
            continue;
        }

        // (c) 其余字节（空白、标点等）：跳过
        ++i;
    }

    // 过滤停用词
    std::vector<std::string> result;
    result.reserve(tokens.size());
    for (const std::string& t : tokens) {
        if (!isStopword(t)) {
            result.push_back(t);
        }
    }
    return result;
}

// 只把 ASCII 大写字母 A-Z 转为小写（'A'+32=='a'）。
// 其他字符（包括汉字的多字节编码）原样保留。
std::string Tokenizer::toLowerAscii(const std::string& text) {
    std::string lower = text;
    for (char& ch : lower) {
        if (ch >= 'A' && ch <= 'Z') {
            ch += static_cast<char>('a' - 'A');   // 大写与小写 ASCII 码相差 32
        }
    }
    return lower;
}

// 判断 text[pos] 处是否是一个 CJK 汉字；若是，通过 len 返回该汉字占用的字节数。
//
// UTF-8 分支：常用（U+4E00 ~ U+9FFF，基本区）编码为 3 字节，
//   首字节范围 0xE4 ~ 0xE9，后两个是 0x80 ~ 0xBF 的"续字节"。
//   （中文标点如"、"是 0xE3 开头，不属于本范围，会在 tokenize 中被跳过。）
//
// GBK 分支：汉字占 2 字节，首字节 0xB0 ~ 0xF7（GB2312 汉字区），
//   尾字节 0x40 ~ 0xFE 且不为 0x7F。
//   （GBK的符号区首字节为 0xA1 ~ 0xA9，不在此范围，同样会被跳过。）
//
// 越界检查：若该位置后面的续字节不足（文本被截断），一律返回 false，
//   由调用方当作"普通字节"跳过，不会越界访问。
bool Tokenizer::isCjkChar(const std::string& text, size_t pos, size_t& len, bool utf8) {
    if (pos >= text.size()) {
        return false;
    }

    const unsigned char c = static_cast<unsigned char>(text[pos]);

    if (utf8) {
        if (c >= 0xE4 && c <= 0xE9 && pos + 2 < text.size()) {
            const unsigned char c1 = static_cast<unsigned char>(text[pos + 1]);
            const unsigned char c2 = static_cast<unsigned char>(text[pos + 2]);
            if (c1 >= 0x80 && c1 <= 0xBF && c2 >= 0x80 && c2 <= 0xBF) {
                len = 3;
                return true;
            }
        }
        return false;
    }

    // GBK 分支
    if (c >= 0xB0 && c <= 0xF7 && pos + 1 < text.size()) {
        const unsigned char c1 = static_cast<unsigned char>(text[pos + 1]);
        if (c1 >= 0x40 && c1 <= 0xFE && c1 != 0x7F) {
            len = 2;
            return true;
        }
    }
    return false;
}

// 启发式判断整段文本是否为合法 UTF-8 编码。
//
// 规则（基础版 UTF-8 校验）：
//   - 0x00 ~ 0x7F：单字节 ASCII，直接通过；
//   - 0xC2 ~ 0xDF：2 字节序列，后面跟 1 个续字节（0x80 ~ 0xBF）；
//   - 0xE0 ~ 0xEF：3 字节序列，后面跟 2 个续字节；
//   - 0xF0 ~ 0xF4：4 字节序列，后面跟 3 个续字节；
//   - 其他首字节（0xC0/0xC1、0xF5~0xFF 等）或续字节不合法 → 返回 false。
//
// 局限：GBK 文本偶尔也可能"碰巧"通过 UTF-8 校验（常见于很短的内容），
//       但对真实语料，GBK 中大量字节组合会产生非法续字节，从而被正确识别。
bool Tokenizer::isUtf8Text(const std::string& text) {
    const size_t n = text.size();
    size_t i = 0;

    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(text[i]);

        if (c <= 0x7F) {
            ++i;                        // 单字节 ASCII
            continue;
        }

        // 根据首字节确定本序列总字节数
        size_t need = 0;
        if (c >= 0xC2 && c <= 0xDF) {
            need = 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            need = 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 4;
        } else {
            return false;               // 非法首字节（如 0xC0、0xC1、0xF5+）
        }

        if (i + need > n) {
            return false;               // 序列被截断，不是完整 UTF-8
        }

        // 检查后续所有"续字节"是否都在 0x80 ~ 0xBF 之间
        for (size_t k = 1; k < need; ++k) {
            const unsigned char cc = static_cast<unsigned char>(text[i + k]);
            if (cc < 0x80 || cc > 0xBF) {
                return false;
            }
        }

        i += need;                      // 跳过整个序列，继续往后扫
    }

    return true;                        // 全部合法，视为 UTF-8
}
