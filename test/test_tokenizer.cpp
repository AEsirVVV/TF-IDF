// test_tokenizer.cpp —— Tokenizer 模块的独立演示/测试程序（带 main()）。
//
// 用途：在 VS Code 中打开本文件，按 F6（C/C++ Compile Run 扩展）即可
//       单独编译运行，快速验证分词器，无需编译整个项目。
//       不要用 F6 直接编译 src/Tokenizer.cpp —— 那是一个没有 main() 的
//       库实现文件，单独链接会报 "undefined reference to WinMain"。
//
// 原理：用"单编译单元"技巧，把 Tokenizer 的实现文件直接包含进来，
//       这样 F6 只需编译这一个文件就能链接出可执行程序：
//         #include "../src/Tokenizer.cpp"
//
// 本文件放在 test/ 目录（不在 src/ 下），Makefile 只编译 src/*.cpp，
// 因此不影响 `mingw32-make` 的项目构建。

#include "Tokenizer.h"        // 通过 VS Code 配置的 -I include 找到
#include "../src/Tokenizer.cpp" // 单编译单元：把实现一起编译进来

#include <cstdlib>       // std::system（Windows 下切换控制台为 UTF-8，避免中文乱码）
#include <filesystem>    // std::filesystem::current_path（提示当前工作目录）
#include <fstream>       // std::ifstream（读取 data/ 样例文档）
#include <iostream>      // std::cout
#include <sstream>       // std::ostringstream

namespace {

// 尝试从几个候选路径加载停用词表（F6 运行时的工作目录不固定，故多试几个）。
// 返回成功使用的路径；全部失败返回空字符串。
std::string loadStopwords(Tokenizer& tok) {
    const char* candidates[] = {
        "stopwords.txt",          // 工作目录 = TF-IDF 项目根目录
        "TF-IDF/stopwords.txt",   // 工作目录 = D:\work\1-TF-IDF
        "data/../stopwords.txt",  // 兜底
    };
    for (const char* p : candidates) {
        if (tok.loadStopwords(p)) {
            return p;
        }
    }
    return "";
}

void printTokens(const char* label, const std::vector<std::string>& tokens) {
    std::cout << label << "（" << tokens.size() << " 个词条）: ";
    for (const std::string& t : tokens) {
        std::cout << "[" << t << "]";
    }
    std::cout << "\n";
}

} // namespace

int main() {
#ifdef _WIN32
    // Windows 控制台默认是 GBK 代码页，把输出代码页切到 UTF-8，避免中文乱码
    std::system("chcp 65001 >nul");
#endif

    std::cout << "===== Tokenizer 演示 =====" << "\n\n";

    // ---------- 1. 基础接口 ----------
    {
        Tokenizer tok;
        printTokens("空字符串        ", tok.tokenize(""));
        tok.addStopword("The"); // 手动加停用词（内部会转小写）
        std::cout << "addStopword(\"The\") 后 isStopword(\"the\") = "
                  << tok.isStopword("the") << "（1=是停用词）\n";
        tok.clearStopwords();
        std::cout << "clearStopwords 后 isStopword(\"the\") = "
                  << tok.isStopword("the") << "（0=不是停用词）\n\n";
    }

    // ---------- 2. 加载项目停用词表 ----------
    Tokenizer tok;
    std::string usedPath = loadStopwords(tok);
    if (usedPath.empty()) {
        std::cout << "[警告] 未找到 stopwords.txt（当前工作目录: "
                  << std::filesystem::current_path() << "），以下演示将不过滤停用词。\n\n";
    } else {
        std::cout << "已加载停用词表: " << usedPath << "\n\n";
    }

    // ---------- 3. 英文 ----------
    printTokens("英文段落        ", tok.tokenize(
        "Artificial intelligence has become one of the most transformative "
        "technologies of the 21st century. From voice assistants like Siri and "
        "Alexa to self-driving cars and medical diagnosis, AI is reshaping every "
        "industry."));

    // ---------- 4. 中文（UTF-8，按单字切分）----------
    printTokens("中文段落        ", tok.tokenize(
        "人工智能正在改变我们的日常生活。从智能语音助手到医疗图像诊断，"
        "AI技术不断突破，但隐私和公平性仍是重要挑战。"));

    // ---------- 5. 中英混合 / 标点 / 数字 ----------
    printTokens("中英混合        ", tok.tokenize("I love C++ and 机器学习 in 2026!"));

    // ---------- 6. 真实样例文档 data/data1.txt ~ data19.txt ----------
    std::cout << "\n----- data/ 样例文档 -----\n";
    for (int i = 1; i <= 19; ++i) {
        // 工作目录不固定，两个候选路径都试一下
        std::ostringstream p1, p2;
        p1 << "data/data" << i << ".txt";
        p2 << "TF-IDF/data/data" << i << ".txt";

        std::ifstream in;
        std::string path;
        in.open(p1.str());
        if (in.is_open()) {
            path = p1.str();
        } else {
            in.open(p2.str());
            if (in.is_open()) path = p2.str();
        }

        if (!in.is_open()) {
            std::cout << "data/data" << i << ".txt  打不开（请确认工作目录）\n";
            continue;
        }
        std::ostringstream content;
        content << in.rdbuf();
        std::vector<std::string> tokens = tok.tokenize(content.str());
        std::cout << path << "  ->  " << tokens.size() << " 个词条";
        if (!tokens.empty()) {
            std::cout << "，前 6 个: ";
            for (size_t k = 0; k < tokens.size() && k < 6; ++k) {
                std::cout << "[" << tokens[k] << "]";
            }
        }
        std::cout << "\n";
    }

    std::cout << "\n===== 演示结束 =====" << std::endl;
    return 0;
}
