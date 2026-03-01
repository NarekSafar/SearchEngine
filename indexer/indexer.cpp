#include "indexer.h"
#include <sstream>
#include <algorithm>
#include <unordered_set>

std::vector<std::string> Indexer::tokenize(const std::string& text) {
    std::stringstream ss(text);
    std::string word;
    std::vector<std::string> tokens;

    static const std::unordered_set<std::string> stopWords = {
        "the","and","is","in","at","on","for","a","an","of"
    };

    while (ss >> word) {
        word.erase(std::remove_if(word.begin(), word.end(), ::ispunct), word.end());
        std::transform(word.begin(), word.end(), word.begin(), ::tolower);

        // if (word.length() < 3) continue;
        if (std::all_of(word.begin(), word.end(), ::isdigit)) continue;
        if (stopWords.find(word) != stopWords.end()) continue;

        tokens.push_back(word);
    }
    return tokens;
}

void Indexer::addDocument(const std::string& url, const std::string& text) {
    auto words = tokenize(text);

    std::unordered_set<std::string> uniqueWords(words.begin(), words.end());

    for (const auto& w : uniqueWords) {
        invertedIndex[w].push_back(url);
    }
}

std::vector<std::string> Indexer::search(const std::string& query) {
    auto words = tokenize(query);
    if (words.empty()) return {};

    if (!invertedIndex.count(words[0])) return {};

    std::vector<std::string> results = invertedIndex[words[0]];

    for (size_t i = 1; i < words.size(); i++) {
        if (!invertedIndex.count(words[i])) return {};

        std::vector<std::string> temp;
        for (auto& url : results) {
            for (auto& u : invertedIndex[words[i]]) {
                if (url == u) {
                    temp.push_back(url);
                }
            }
        }
        results = temp;
    }

    return results;
}
