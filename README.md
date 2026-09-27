# 基于 TF-IDF 的纯 C++ 智能检索引擎及 Web 可视化面板

一个基于 TF-IDF（词频-逆文档频率） 算法的中英文智能检索引擎：
后端为纯 C++17（含 HTTP 服务），前端为原生 HTML/JS + Chart.js 可视化面板。

## 功能特性
- 纯 C++ 后端四模块：`FileReader → Tokenizer → InvertedIndex → SearchEngine`
- TF-IDF 权重 + 余弦相似度 + 堆排序 Top-K（返回 Top-5）
- 支持中英文：英文按词切分、中文按单字切分，UTF-8 / GBK 自动识别
- 中文停用词过滤、大小写归一化
- 倒排索引预筛选候选文档（避免全量扫描）
- **多线程并行构建**：自实现 `ThreadPool`（任务队列 + 互斥锁 + 条件变量 +
  `std::future` + 优雅退出），一个线程池贯穿 `build()` 的三个阶段
  （分词、算 df、造 TF-IDF 向量），各线程只写自己的下标、全程无锁，
  唯一的共享结构（倒排索引 + 词典）留在单线程合并；线程数取
  `min(硬件并发度, 文档数)`；并按语料规模分流——文档数 < 256 走串行
  （并行有约 1.3 ms 固定开销，小语料下反而更慢），≥ 256 才启用线程池
  （实测 500 篇 1.8 倍、2000 篇 2.4 倍，结果与串行逐位一致）
- 相关度判断：每条结果标记"✓ 相关 / ✗ 不相关"（是否包含查询全部词条），
  并统计前三篇正确率；排序带"查询覆盖度惩罚"，保证相关文档排前面
- Web 前端：文档库、搜索框、相似度柱状图、原文查看弹窗

## 环境要求
- Windows + MinGW-w64 GCC（16.x，支持 C++17）+ GNU Make（`mingw32-make`）
- 无需安装其他依赖；前端 Chart.js 走 CDN，首次打开页面需联网

> 说明：项目主要在 Windows 下开发验证；Makefile 含 Linux 分支，理论可编译，
> 未在 Linux 上实测。

## 第三方依赖与致谢

本项目使用了以下第三方资源，向原作者致谢：

- **cpp-httplib**（`include/httplib.h`）：HTTP 服务使用的单头文件库，来源于
  [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib/blob/master/httplib.h)
  （MIT 协议），已直接置于 `include/` 目录。
- **停用词表**（`stopwords.txt`）：中英文停用词过滤使用，来源于
  [217heidai/stopwords](https://raw.githubusercontent.com/217heidai/stopwords/main/stopwords/stopwords.txt)
  ，已随仓库提供（3328 词，UTF-8），无需联网下载。
- **Chart.js**：前端相似度柱状图绘制，通过 CDN 加载，来源于
  [Chart.js 官网](https://www.chartjs.org)（MIT 协议）。

## 快速开始
```bat
:: 1. 克隆仓库
git clone <仓库地址> TF-IDF
cd TF-IDF

:: 2. 设置 MinGW 根目录（安装位置中含 bin/ 的目录）
set MINGW_HOME=C:\你的路径\mingw64

:: 3. 构建并运行
mingw32-make run

:: 4. 浏览器打开
::    http://localhost:8080
```

- 仓库不含编译产物（`output/` 等已被 .gitignore 忽略），首次使用需自行
  安装 MinGW-w64；
- `MINGW_HOME` 是唯一需要按机器设置的配置（环境变量或命令行覆盖均可，
  例如 `mingw32-make MINGW_HOME=C:/msys64/mingw64`）；Makefile 中已含默认值
  示例，可自行修改；
- `main.exe` 支持从任意目录启动（自动定位 data / web / stopwords），
  无需修改代码路径。

## 构建与运行
```bat
mingw32-make          :: 构建（产物 output\main.exe）
mingw32-make run      :: 构建并运行
mingw32-make clean    :: 清理 .o / .d / main.exe
```

> 提示：先停止旧的服务进程再重新构建，否则 `output/main.exe` 被占用会报
> `Permission denied`。`output/` 中需包含 MinGW 运行时 DLL
> （`libstdc++-6.dll`、`libgcc_s_seh-1.dll`、`libwinpthread-1.dll`），
> 否则 exe 无法直接双击运行。

## 使用与接口
启动后服务监听 `8080` 端口，提供以下接口：

- `GET /`：前端搜索页面
- `GET /search?q=关键词`：检索，返回 Top-5（含相关度判断）
- `GET /doc?name=data6.txt`：单篇文档原文（不存在返回 404）
- `GET /docs`：全部文档列表（左侧"文档库"面板用）

`/search` 返回示例：

```json
[{"doc_name":"data1.txt","score":0.578807,"relevant":true},
 {"doc_name":"data6.txt","score":0.089268,"relevant":true}]
```

推荐试搜：`机器学习`、`足球` / `football`、`健康` / `health`、`太空`、
`美食`、`economy`、`AI`、`climate change`；扩充语料中的新主题同样可搜，
例如 `茶文化`、`铁路`、`starter`、`木工`、`telescope`。

## 性能实测（本机 32 逻辑核，500 篇语料，取 5 次中位数）

| 指标 | 数值 |
|---|---|
| 索引构建（串行 / 并行） | 13.6 ms / 7.3 ms → **1.8 倍** |
| 索引构建（2000 篇，串行 / 并行） | 53.7 ms / 22.0 ms → **2.4 倍** |
| 单次检索延迟（500 篇） | 0.002 ~ 0.12 ms（亚毫秒） |
| 构建结果一致性 | 串行 vs 并行逐名次分数差 0（完全一致） |

构建耗时拆解（500 篇串行）：分词 3.9 ms、倒排索引 2.4 ms、算 df 0.7 ms、
造 TF-IDF 向量 5.4 ms——造向量最重，因此它和分词、算 df 都做了并行，
倒排索引涉及共享 map 的写入，保留在单线程（这是当前 Amdahl 瓶颈所在）。

## 测试与验证
- 页面内置：每次搜索后，结果行显示 ✓/✗ 相关徽标，并给出前三篇正确率；
- 单元级：编译运行 `test/test_verify.cpp`（57 条金标准断言：20 条原始题材 +
  20 条扩充题材 top-1 + 8 条全相关不变量 + 5 条边界 + 4 条结构断言，全过退出码 0）；
- 端到端：服务运行中执行 `powershell -File test\verify_http.ps1`（45 条断言：
  40 条题材 top-1 + 5 条边界，对 /search 发真实 HTTP 请求，全过退出码 0）；
- `test/` 下的其他演示程序（test_tokenizer / test_invertedindex /
  test_searchengine / test_filereader / test_threadpool）自带 `main()`，
  可单独编译运行，演示各模块行为（含线程池与并行构建）。

## 项目结构

```
├── Makefile              构建脚本（MINGW_HOME 可移植配置）
├── src/                  后端源码（main.cpp + 四个业务模块 + ThreadPool.cpp）
├── include/              头文件（五个模块 + httplib.h）
├── test/                 各模块演示程序 + 验证工具（含 test_threadpool.cpp）
├── web/index.html        前端页面
├── data/                 500 篇语料、93 个题材（原始 19 篇 13 个题材 +
│                         扩充 481 篇 80 个新题材，中英各半、UTF-8，词典 2205 词）
├── lib/                  第三方库（当前为空）
└── stopwords.txt         停用词表（3328 词，UTF-8）
```

（`output/`、`*.o`、`*.exe` 等构建产物，以及 `.vscode/`、`.claude/` 本机配置
均不入库，已由 .gitignore 忽略。）

## 技术栈
- **语言**：C++17（纯 STL）
- **并发**：`std::thread` + `std::mutex` + `std::condition_variable` +
  `std::future`（自实现线程池，用于并行构建索引）
- **HTTP**：cpp-httplib（单头文件，MIT）
- **构建**：GNU Make + MinGW-w64
- **前端**：HTML / JS / Chart.js（CDN）
- **算法**：TF-IDF + 余弦相似度 + 查询覆盖度惩罚 + 堆排序 Top-K
