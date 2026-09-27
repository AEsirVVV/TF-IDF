#ifndef SEARCHENGINE_H
#define SEARCHENGINE_H

#include "FileReader.h"   // struct Document（FR1：文档数据模型，定义见 FileReader.h）
#include "InvertedIndex.h"
#include "Tokenizer.h"

#include <string>
#include <unordered_map>
#include <vector>

// SearchResult：一条检索结果。
struct SearchResult {
    std::string docName;    // 命中文档名
    double score;           // 余弦相似度分数（0~1，可 *100 显示为百分比）
    bool relevant = false;  // 相关度判断：该文档是否包含查询的"全部"词条
                            // （前端据此计算前 3 篇的正确率）
};

// SearchEngine：TF-IDF 计算 + 余弦相似度 + Top-K 排名。
//   1. build()：把整批文档分词建索引，并转换为 TF-IDF 向量集（向量化）；
//   2. search()：查询串同样向量化，先经倒排索引预筛选候选文档，再对候选文档计算余弦相似度，最后用堆排序（std::priority_queue）
//      取出相似度最高的 K 个结果。

class SearchEngine {
public:
    // 构造时初始化内部分词器
    // 加载停用词表；加载失败不影响运行，只是不过滤停用词，可随后用 loadStopwords 重新加载。
    explicit SearchEngine(const std::string& stopwordsFilePath = "stopwords.txt");

    // 重新加载停用词表，传给内部 Tokenizer，返回是否成功。
    bool loadStopwords(const std::string& stopwordsFilePath);

    // 用整批文档构建倒排索引与 TF-IDF 向量，可重复调用（自动重建）。
    void build(const std::vector<Document>& docs);

    // 检索：query 为原始查询串，topK 为返回结果数上限（项目约定 K=5）。
    // 返回按相似度从高到低排序的结果；空查询 / 纯停用词查询 / 无匹配时返回空列表。
    std::vector<SearchResult> search(const std::string& query, int topK) const;

    // 文档总数 N（IDF 公式用；也供测试观察）。
    size_t documentCount() const;

    // 词典大小（TF-IDF 向量维度）。
    size_t vocabularySize() const;

    // 构建时使用的线程数（线程池大小）；build() 之前或语料为空时为 0。
    // 供测试/基准程序观察并行度。
    size_t threadCount() const;

private:
    // 计算两个等长 TF-IDF 向量的余弦相似度。
    // 零向量视为无特征，返回 0。
    static double computeCosineSimilarity(const std::vector<double>& v1, const std::vector<double>& v2);

    // 经典 IDF 公式：idf = ln(N / df)，df 为该词文档频率。
    // 防御：df <= 0（词不在任何文档中）返回 0，使该词权重为 0。
    double computeIdf(int df) const;

    // 通过文档 ID 查找文件名（线性扫描 docs_，文档数少，够用）。
    std::string docNameById(int docId) const;

    // 相关度判断：文档是否包含查询的"全部"词条。
    // 定义：一篇文档只有同时包含查询的所有词条才算"符合"（例如查询"足球"，
    // 只含"球"的"月球"文档不算符合）。供 search() 填充 SearchResult.relevant。
    bool containsAllTerms(int docId, const std::vector<std::string>& terms) const;

    Tokenizer tokenizer_;      // 内部分词器（含停用词表）
    InvertedIndex index_;      // 内部倒排索引（候选集预筛选）
    std::vector<Document> docs_; // 文档元信息（ID -> 文件名）
    int totalDocs_ = 0;        // 文档总数 N
    size_t vocabSize_ = 0;     // 词典大小（TF-IDF 向量维度）
    size_t poolSize_ = 0;      // 构建时线程池大小（并行分词用）

    // 词典映射：词 -> 向量维度下标。
    std::unordered_map<std::string, int> termToDim_;
    // 词 -> 包含该词的文档数（文档频率 df），用于 IDF。
    std::unordered_map<std::string, int> documentFrequency_;
    // 文档 ID -> TF-IDF 稠密向量（长度 = vocabSize_）。
    std::unordered_map<int, std::vector<double>> docVectors_;
};

#endif // SEARCHENGINE_H
