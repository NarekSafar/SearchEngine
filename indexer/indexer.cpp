#include "indexer.h"
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <iostream>

Indexer::Indexer(const std::string& fname) : filename(fname) {}

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
        if (stopWords.find(word) != stopWords.end()) continue;
        tokens.push_back(word);
    }

    return tokens;
}

void Indexer::addDocument(const std::string& url, const std::string& text) {
    auto words = tokenize(text);
    std::unordered_set<std::string> uniqueWords(words.begin(), words.end());

    std::unordered_map<std::string, std::vector<std::string>> index;

    int fd = open(filename.c_str(), O_RDONLY);
    if (fd >= 0) {
        while (true) {
            uint64_t wordLen;
            ssize_t r = read(fd, &wordLen, sizeof(wordLen));
            if (r <= 0) break;

            std::string word(wordLen, ' ');
            read(fd, &word[0], wordLen);

            uint64_t numUrls;
            read(fd, &numUrls, sizeof(numUrls));

            std::vector<std::string> urls;
            for (uint64_t i = 0; i < numUrls; ++i) {
                uint64_t urlLen;
                read(fd, &urlLen, sizeof(urlLen));
                std::string u(urlLen, ' ');
                read(fd, &u[0], urlLen);
                urls.push_back(u);
            }

            index[word] = urls;
        }
        close(fd);
    }

    for (auto& w : uniqueWords) {
        auto& urls = index[w];
        if (std::find(urls.begin(), urls.end(), url) == urls.end()) {
            urls.push_back(url);
        }
    }

    fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;

    for (auto& [word, urls] : index) {
        uint64_t wordLen = word.size();
        write(fd, &wordLen, sizeof(wordLen));
        write(fd, word.c_str(), wordLen);

        uint64_t numUrls = urls.size();
        write(fd, &numUrls, sizeof(numUrls));

        for (auto& u : urls) {
            uint64_t urlLen = u.size();
            write(fd, &urlLen, sizeof(urlLen));
            write(fd, u.c_str(), urlLen);
        }
    }

    close(fd);
}

std::vector<std::string> Indexer::search(const std::string& query) {
    auto words = tokenize(query);
    if (words.empty()) return {};

    std::unordered_map<std::string, std::vector<std::string>> index;
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0) return {};

    while (true) {
        uint64_t wordLen;
        ssize_t r = read(fd, &wordLen, sizeof(wordLen));
        if (r <= 0) break;

        std::string word(wordLen, ' ');
        read(fd, &word[0], wordLen);

        uint64_t numUrls;
        read(fd, &numUrls, sizeof(numUrls));

        std::vector<std::string> urls;
        for (uint64_t i = 0; i < numUrls; ++i) {
            uint64_t urlLen;
            read(fd, &urlLen, sizeof(urlLen));
            std::string u(urlLen, ' ');
            read(fd, &u[0], urlLen);
            urls.push_back(u);
        }

        index[word] = urls;
    }

    close(fd);

    if (index.find(words[0]) == index.end()) return {};
    std::vector<std::string> results = index[words[0]];

    for (size_t i = 1; i < words.size(); ++i) {
        if (index.find(words[i]) == index.end()) return {};
        std::vector<std::string> temp;
        for (auto& u1 : results) {
            for (auto& u2 : index[words[i]]) {
                if (u1 == u2) temp.push_back(u1);
            }
        }
        results = temp;
        if (results.empty()) break;
    }
    return results;
}
