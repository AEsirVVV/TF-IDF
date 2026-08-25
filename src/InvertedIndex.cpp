// InvertedIndex.cpp —— 倒排索引模块的实现文件。
//
// 核心数据结构：
//   invertedIndex_ : unordered_map<词条, vector<文档ID>>   —— 倒排表
//   docWordCount_  : unordered_map<文档ID, 总词数>          —— 文档长度
//
// 注：倒排表中的文档 ID 未去重（同一文档出现 N 次该词条就写 N 个 ID）。

#include "InvertedIndex.h"

#include <algorithm> 

// 向索引中添加一篇文档。
//
// 算法（单遍扫描）：
//   1. 防御性检查：同一 docId 只允许索引一次（文档 ID 应当唯一），重复添加直接忽略，避免倒排表被重复写坏；
//   2. 记录文档总词数（文档长度），供后续 TF 归一化使用；
//   3. 遍历每个词条，把 docId 追加到该词条的倒排列表末尾（同一词条出现几次就追加几次）。

void InvertedIndex::addDocument(int docId, const std::vector<std::string>& tokens) {
    // 同一个文档 ID 只索引一次
    if (docWordCount_.find(docId) != docWordCount_.end()) {
        return;
    }

    // 记录文档总词数（空文档记为 0，但 docId 仍然存在）
    docWordCount_[docId] = static_cast<int>(tokens.size());

    // 逐词条写入倒排表
    for (const std::string& token : tokens) {
        if (token.empty()) {
            continue;               // 防御：跳过空词条
        }
        invertedIndex_[token].push_back(docId);
    }
}

// 检索候选集：返回至少包含一个查询词条的文档 ID。
//
// 算法（基础做法：收集 -> 排序 -> 去重）：
//   1. 对每个查询词条，找到它的倒排列表，把里面的文档 ID 全部收集起来；
//   2. 一次 std::sort 升序排序；
//   3. std::unique 把相邻重复项"挤"到尾部并 erase 掉，实现去重。

std::vector<int> InvertedIndex::getCandidates(const std::vector<std::string>& queryTerms) const {
    std::vector<int> result;
    if (queryTerms.empty()) {
        return result;              // 空查询 → 空候选集
    }

    // 1. 收集所有命中文档 ID（查询词条重复也没关系，最后统一去重）
    for (const std::string& term : queryTerms) {
        auto it = invertedIndex_.find(term);      // 哈希查找 O(1) 平均
        if (it == invertedIndex_.end()) {
            continue;               // 词条不在词典中，跳过
        }
        const std::vector<int>& postings = it->second;
        result.insert(result.end(), postings.begin(), postings.end());
    }

    // 2. 升序排序 + 相邻去重
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());

    return result;
}
 
// 返回某篇文档的总词数（文档长度）。
// 未索引过的文档返回 0（用 find 而非 []，避免向 map 中误插入条目）。
int InvertedIndex::getDocWordCount(int docId) const {
    auto it = docWordCount_.find(docId);
    if (it == docWordCount_.end()) {
        return 0;
    }
    return it->second;
}

// 返回词条 term 在文档 docId 中的词频（出现次数）。
//
// 算法：线性扫描 term 的倒排列表，数一数 docId 出现了多少次。
// 这正是"倒排表不去重"设计带来的好处，词频信息天然存在列表里。
int InvertedIndex::getTermFrequency(int docId, const std::string& term) const {
    auto it = invertedIndex_.find(term);
    if (it == invertedIndex_.end()) {
        return 0;
    }

    int count = 0;
    for (int id : it->second) {
        if (id == docId) {
            ++count;
        }
    }
    return count;
}

// 返回词条 term 的文档频率 df（出现在几篇文档中，即文档数，按文档去重）。
//
// 算法：把倒排列表复制一份，排序 + 去重后数个数。
// 之所以不能直接返回列表长度，是因为列表里同一文档可能出现多次；
int InvertedIndex::getDocumentFrequency(const std::string& term) const {
    auto it = invertedIndex_.find(term);
    if (it == invertedIndex_.end()) {
        return 0;
    }

    std::vector<int> postings = it->second;       // 复制一份再操作
    std::sort(postings.begin(), postings.end());    //排序
    postings.erase(std::unique(postings.begin(), postings.end()), postings.end());  //去重，只看出现在哪些文档

    return static_cast<int>(postings.size());
}

// 返回词典大小（不同词条总数），即倒排表的大小。
size_t InvertedIndex::vocabularySize() const {
    return invertedIndex_.size();
}

// 清空索引中的所有数据。直接把两个 map 清空即可（O(词条数+文档数)）。
void InvertedIndex::clear() {
    invertedIndex_.clear();
    docWordCount_.clear();
}
