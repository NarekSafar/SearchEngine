#ifndef INDEXER_H
#define INDEXER_H

#include <string>
#include <vector>
#include <unordered_set>
#include <libstemmer.h>
#include <stdint.h>

struct Bucket {
    int64_t offset;
};

struct LogEntry {
    char word[32];	
    uint64_t linkID;
    int64_t prevOffset;
};

class Indexer {
public:
    Indexer(const std::string& hashFile, const std::string& logFile);
    ~Indexer();

    void addDocument(uint64_t linkID, const std::string& text);
    std::vector<uint64_t> search(const std::string& query);
    void compact();

private:
    std::string hashFilename;
    std::string logFilename;

    sb_stemmer* stemmer;

    static const uint64_t BUCKET_COUNT = 1000003;

    uint64_t hashFunction(const std::string& key);
    std::vector<std::string> tokenize(const std::string& text);
};

#endif
