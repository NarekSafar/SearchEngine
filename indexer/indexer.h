#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include <libstemmer.h>

class Indexer {
public:
    struct SearchResult {
        uint64_t linkID;
        double score;
        std::vector<std::string> missingWords;
    };

    Indexer(const std::string& hashFile, const std::string& logFile);
    ~Indexer();

    void addDocument(uint64_t linkID, const std::string& text);
    void compact();
    std::vector<SearchResult> search(const std::string& query, const std::string& mode);

private:
    struct Bucket {
        int64_t offset;
    };

    struct LogEntry {
        char word[64];
        uint64_t linkID;
        int64_t prevOffset;
    };

    static const uint64_t BUCKET_COUNT = 10007;

    std::string hashFilename;
    std::string logFilename;

    sb_stemmer* stemmer;

    uint64_t hashFunction(const std::string& key);
    std::vector<std::string> tokenize(const std::string& text);
};
