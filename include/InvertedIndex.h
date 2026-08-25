#ifndef INVERTEDINDEX_H
#define INVERTEDINDEX_H

#include <string>
#include <unordered_map>
#include <vector>

// InvertedIndex：倒排索引。
// 负责在 Tokenizer 分词结果之上，构建"词条 -> 出现该词条的文档列表"，
// 供后续 SearchEngine 做 TF-IDF 计算与检索预筛选（候选集）。
//
// 设计要点：
//   1. invertedIndex_：unordered_map<词条, vector<文档ID>>，即倒排表。
//      这里采用最基础的做法：文档中每出现一次该词条，就追加一个文档 ID。
//      因此同一文档 ID 在倒排表中可能出现多次，含义如下：
//        - 词频 tf(词, 文档) = 该文档 ID 在倒排表中出现的次数；
//        - 文档频率 df(词)   = 倒排表"去重后"的文档数量。
//   2. docWordCount_：unordered_map<文档ID, 总词数>，即每篇文档的长度，
//      用于 TF 归一化（tf = 词频 / 文档总词数）。

class InvertedIndex {
public:
    InvertedIndex() = default;

    // 向索引中添加一篇文档。
    // tokens 应为 Tokenizer::tokenize 的输出（已小写、已过滤停用词）。
    // 约定：每个文档 ID 只应添加一次；重复添加同一 docId 会被忽略。
    void addDocument(int docId, const std::vector<std::string>& tokens);

    // 检索候选集：返回至少包含一个查询词条的文档 ID 列表。
    // 结果已去重并按升序排序；queryTerms 为空或无匹配时返回空列表。
    std::vector<int> getCandidates(const std::vector<std::string>& queryTerms) const;

    // 返回某篇文档的总词数（文档长度）；未添加过的文档返回 0。
    int getDocWordCount(int docId) const;

    // 返回词条 term 在文档 docId 中的词频（出现次数）；不存在返回 0。
    int getTermFrequency(int docId, const std::string& term) const;

    // 返回词条 term 出现在几篇文档中（文档频率 df，用于 IDF 计算；
    // 按文档去重统计，与倒排表里的重复条目无关）。不存在返回 0。
    int getDocumentFrequency(const std::string& term) const;

    // 返回词典大小（索引中不同词条的总数）。
    size_t vocabularySize() const;

    // 清空索引中的所有数据（倒排表 + 文档长度表）。
    // 供 SearchEngine::build 重建索引时使用。
    void clear();

private:
    // 倒排表：词条 -> 文档 ID 列表（可能含重复，见类注释）。
    std::unordered_map<std::string, std::vector<int>> invertedIndex_;

    // 每篇文档的总词数：文档ID -> 词条数。
    std::unordered_map<int, int> docWordCount_;
};

#endif // INVERTEDINDEX_H
