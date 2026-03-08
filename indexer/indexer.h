#pragma once
#include <string>
#include <vector>

class Indexer {
public:
    Indexer(const std::string& fname);
    void addDocument(const std::string& url, const std::string& text);
    std::vector<std::string> search(const std::string& query);

private:
    std::string filename;
    std::vector<std::string> tokenize(const std::string& text);
};
