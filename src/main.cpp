// main.cpp —— TF-IDF 检索引擎 HTTP 服务。
//
// 启动流程：
//   1. FileReader 读取 data/ 下所有 .txt 文档；
//   2. SearchEngine 加载停用词表并构建倒排索引 + TF-IDF 向量；
//   3. 启动 cpp-httplib 服务器：
//
// 依赖：项目自带四个模块（FileReader/Tokenizer/InvertedIndex/SearchEngine）+ 单头文件库 httplib.h，无任何第三方依赖。
//
// 编码说明：本文件必须保存为 UTF-8 编码（.vscode/settings.json 已固定"files.encoding": "utf8"）。
#include "FileReader.h"
#include "SearchEngine.h"
#include "httplib.h"

#include <cstdio>      // std::snprintf
#include <filesystem>  // std::filesystem（解析项目根目录）
#include <iostream>    // std::cout
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h> // SetConsoleOutputCP / CP_UTF8
#endif

namespace {

// 解析项目根目录（同时包含 data/、web/、stopwords.txt 的目录）。
// 让 main.exe 无论从哪个工作目录启动（项目根 / output/ 下双击 /
// 上级目录 / 调试器），都能找到数据与静态资源。
// 候选顺序：
//   1. 可执行文件所在目录的上一级（<根>/output/main.exe -> <根>）；
//   2. 可执行文件所在目录本身（若 exe 被直接放在根目录）；
//   3. 当前工作目录；
//   4. 当前工作目录下的 TF-IDF 子目录（从外层目录启动时）。
std::filesystem::path findProjectRoot() {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;

#ifdef _WIN32
    // 用 Windows API 拿 exe 的完整路径（比 argv[0] 更可靠）
    wchar_t buf[MAX_PATH];
    if (::GetModuleFileNameW(nullptr, buf, MAX_PATH) > 0) {
        fs::path exeDir = fs::path(buf).parent_path();
        if (exeDir.filename() == "output") {
            candidates.push_back(exeDir.parent_path());   // <根>/output -> <根>
        }
        candidates.push_back(exeDir);
    }
#endif

    candidates.push_back(fs::current_path());
    candidates.push_back(fs::current_path() / "TF-IDF");

    // 取第一个"三个关键资源都在"的候选；都找不到就退回第一个（main 里会警告）
    for (const fs::path& c : candidates) {
        if (fs::is_directory(c / "data") && fs::is_directory(c / "web") &&
            fs::is_regular_file(c / "stopwords.txt")) {
            return c;
        }
    }
    return candidates.front();
}

// 把 double 格式化成"尽量短"的十进制字符串："0.369100" -> "0.3691"；"1.000000" -> "1"
std::string formatDouble(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", value);
    std::string s(buf);

    const size_t dot = s.find('.');
    if (dot != std::string::npos) {
        const size_t lastNonZero = s.find_last_not_of('0');
        if (lastNonZero > dot) {
            s.erase(lastNonZero + 1);      // 去掉末尾的 '0'
        } else if (lastNonZero == dot) {
            s.erase(dot);                  // "1.000000" -> "1"
        }
    }
    return s;
}

// 字符串转义：把任意文本转成合法的 JSON 字符串内容。
// 除了引号和反斜杠，还要转义换行/回车/制表符等控制字符，
// 因为 /doc 接口会把整篇文档内容放进 JSON 字符串（文档里可能含有换行）。
std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // 其他控制字符转成 \u00XX，保证 JSON 始终合法
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;   // 普通字符（含 UTF-8 中文多字节）原样保留
                }
        }
    }
    return out;
}

// 单篇文档转 JSON：{"doc_name":"data1.txt","content":"<全文>"}，供 /doc 接口使用。
std::string docToJson(const Document& doc) {
    return "{\"doc_name\":\"" + jsonEscape(doc.name) + "\","
           "\"content\":\"" + jsonEscape(doc.content) + "\"}";
}

} // namespace

int main() {
#ifdef _WIN32
    // 把控制台输出代码页切到 UTF-8：源码中的中文字符串按 UTF-8 字节输出后，
    // 控制台按 UTF-8 解释，中文日志不再乱码（等效于手动执行 chcp 65001）。
    SetConsoleOutputCP(CP_UTF8);
#endif

    // ---------- 0. 解析项目根目录（路径与启动位置无关） ----------
    const std::filesystem::path root = findProjectRoot();
    std::cout << "项目根目录: " << root.string() << std::endl;
    const std::string dataDir       = (root / "data").string();
    const std::string webDir        = (root / "web").string();
    const std::string stopwordsFile = (root / "stopwords.txt").string();

    // ---------- 1. 读取语料 ----------
    FileReader reader;
    std::vector<Document> docs = reader.readAllDocuments(dataDir);
    if (docs.empty()) {
        std::cout << "[警告] 项目根目录的 data/ 下没有找到 .txt 文档，检索将始终无结果。" << std::endl;
    } else {
        std::cout << "已读取 " << docs.size() << " 篇文档：" << std::endl;
    }

    // ---------- 2. 构建检索引擎 ----------
    SearchEngine engine(stopwordsFile);
    engine.build(docs);
    std::cout << "索引构建完成：词典大小 = " << engine.vocabularySize() << std::endl;

    // ---------- 3. HTTP 服务 ----------
    httplib::Server svr;

    // 静态文件：/ -> web/index.html（前端页面）。注意 cpp-httplib 的路由顺序是
    // "先试静态文件、文件不存在再走显式路由"，所以 /search 不会被这里吞掉。
    if (!svr.set_mount_point("/", webDir)) {
        std::cout << "[警告] 挂载 web/ 目录失败：" << webDir << std::endl;
    }

    // 检索接口：GET /search?q=关键词
    // 每条结果附带 relevant 字段（该文档是否包含查询的全部词条），
    // 前端据此判断"前三篇是否符合"并计算正确率。
    svr.Get("/search", [&](const httplib::Request& req, httplib::Response& res) {
        const std::string q = req.get_param_value("q");
        // Top-K = 5（需求 FR5 约定 K=5）
        const std::vector<SearchResult> results = engine.search(q, 5);
        std::string json = "[";
        for (size_t i = 0; i < results.size(); ++i) {
            if (i > 0) {
                json += ",";
            }
            json += "{\"doc_name\":\"" + jsonEscape(results[i].docName) + "\","
                    "\"score\":" + formatDouble(results[i].score) + ","
                    "\"relevant\":" + (results[i].relevant ? "true" : "false") + "}";
        }
        json += "]";
        res.set_content(json, "application/json; charset=utf-8");
    });

    // 文档原文接口
    // 供前端"查看原文"弹窗使用；文档不存在返回 404 + JSON 错误信息。
    svr.Get("/doc", [&](const httplib::Request& req, httplib::Response& res) {
        const std::string name = req.get_param_value("name");
        for (const Document& doc : docs) {
            if (doc.name == name) {
                res.set_content(docToJson(doc), "application/json; charset=utf-8");
                return;
            }
        }
        res.status = 404;
        res.set_content("{\"error\":\"文档不存在: " + jsonEscape(name) + "\"}",
                        "application/json; charset=utf-8");
    });

    svr.Get("/docs", [&](const httplib::Request&, httplib::Response& res) {
        std::string json = "[";
        for (size_t i = 0; i < docs.size(); ++i) {
            if (i > 0) {
                json += ",";
            }
            json += "{\"doc_name\":\"" + jsonEscape(docs[i].name) + "\","
                    "\"size\":" + std::to_string(docs[i].content.size()) + "}";
        }
        json += "]";
        res.set_content(json, "application/json; charset=utf-8");
    });

    std::cout << "\nTF-IDF 检索引擎已启动：" << std::endl;
    std::cout << "  前端页面 : http://localhost:8080/" << std::endl;
    std::cout << "按 Ctrl+C 停止服务。\n" << std::endl;

    svr.listen("0.0.0.0", 8080);
    return 0;
}
