#ifndef INDEXER_H
#define INDEXER_H

#include <string>
#include <vector>
#include <cstdint>
#include <libstemmer.h>

class Indexer {
public:
    explicit Indexer(const std::string& fname);
    ~Indexer();
    void addDocument(const std::string& url, const std::string& text);
    std::vector<std::string> search(const std::string& query);

private:
    std::string filename;
    static const uint64_t BUCKET_COUNT = 10007; 

    struct sb_stemmer* stemmer;

    std::vector<std::string> tokenize(const std::string& text);
    uint64_t hashFunction(const std::string& key);
    
    struct Bucket {
        uint64_t offset;
    };

    struct NodeHeader {
        uint64_t wordLen;
        uint64_t numUrls;
        uint64_t nextNodeOffset; 
    };
};

#endif
