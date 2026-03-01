#ifndef INDEXER_H
#define INDEXER_H

#include <string>
#include <unordered_map>
#include <vector>

class Indexer {
public:
    void addDocument(const std::string& url, const std::string& text);
    std::vector<std::string> search(const std::string& query);

private:
    std::unordered_map<std::string, std::vector<std::string>> invertedIndex;
    std::vector<std::string> tokenize(const std::string& text);
};

#endif
