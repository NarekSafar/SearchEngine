#include "indexer.h"

#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sys/stat.h>


Indexer::Indexer(const std::string& hashFile, const std::string& logFile)
    : hashFilename(hashFile), logFilename(logFile)
{
    stemmer = sb_stemmer_new("english", NULL);

    if (stemmer == nullptr) {
        std::cerr << "Error: Could not initialize libstemmer.\n";
    }

    int fd = open(hashFilename.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
    if (fd >= 0) {
        Bucket empty = {-1};

        for (uint64_t i = 0; i < BUCKET_COUNT; ++i) {
            write(fd, &empty, sizeof(Bucket));
        }

        close(fd);
    }

    int logFd = open(logFilename.c_str(), O_RDWR | O_CREAT, 0644);
    if (logFd >= 0) close(logFd);
}


Indexer::~Indexer()
{
    if (stemmer) {
        sb_stemmer_delete(stemmer);
        stemmer = nullptr;
    }
}


uint64_t Indexer::hashFunction(const std::string& key)
{
    uint64_t hash = 5381;

    for (char c : key)
        hash = ((hash << 5) + hash) + c;

    return hash % BUCKET_COUNT;
}


std::vector<std::string> Indexer::tokenize(const std::string& text)
{
    std::stringstream ss(text);
    std::string word;
    std::vector<std::string> tokens;

    static const std::unordered_set<std::string> stopWords = {
    "the","and","is","in","at","on","for","a","an","of",
    "to","from","by","with","about","as","into","over","after",

    "i","you","he","she","it","we","they",
    "me","him","her","us","them",
    "my","your","his","their","our",

    "this","that","these","those",
    "there","here",

    "what","which","who","when","where","why","how",

    "be","am","are","was","were","been","being",
    "have","has","had","do","does","did",
    "will","would","can","could","should","may","might",

    "not","no","yes","ok","okay"
};

    while (ss >> word) {

        word.erase(std::remove_if(word.begin(), word.end(), ::ispunct), word.end());

        std::transform(word.begin(), word.end(), word.begin(), ::tolower);

        if (word.empty() || stopWords.count(word))
            continue;

        if (stemmer) {
            const sb_symbol* stemmed = sb_stemmer_stem(
                stemmer,
                reinterpret_cast<const sb_symbol*>(word.c_str()),
                word.length()
            );

            if (stemmed) {
                tokens.push_back(reinterpret_cast<const char*>(stemmed));
            }
        }
        else {
            tokens.push_back(word);
        }
    }

    return tokens;
}


void Indexer::addDocument(uint64_t linkID, const std::string& text)
{
    auto words = tokenize(text);

    std::unordered_set<std::string> uniqueWords(words.begin(), words.end());

    int hashFd = open(hashFilename.c_str(), O_RDWR);
    int logFd = open(logFilename.c_str(), O_RDWR);

    if (hashFd < 0 || logFd < 0) {
        std::cerr << "Error opening files.\n";
        return;
    }

    for (const auto& word : uniqueWords) {

        uint64_t idx = hashFunction(word);
        uint64_t pos = idx * sizeof(Bucket);

        Bucket b;

        lseek(hashFd, pos, SEEK_SET);
        read(hashFd, &b, sizeof(Bucket));

        int64_t prev = b.offset;

        int64_t newOffset = lseek(logFd, 0, SEEK_END);

        LogEntry entry;
        strncpy(entry.word, word.c_str(), sizeof(entry.word));
        entry.word[sizeof(entry.word) - 1] = '\0';

        entry.linkID = linkID;
        entry.prevOffset = prev;

        write(logFd, &entry, sizeof(entry));

        lseek(hashFd, pos, SEEK_SET);
        write(hashFd, &newOffset, sizeof(int64_t));
    }

    close(hashFd);
    close(logFd);
}


void Indexer::compact()
{
    int oldFd = open(logFilename.c_str(), O_RDONLY);
    if (oldFd < 0) return;

    int newFd = open("posts_new.log", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (newFd < 0) {
        close(oldFd);
        return;
    }

    std::unordered_map<std::string, LogEntry> latest;

    off_t offset = 0;
    LogEntry entry;

    while (pread(oldFd, &entry, sizeof(entry), offset) == sizeof(entry)) {

        std::string key = std::string(entry.word) + "_" + std::to_string(entry.linkID);

        latest[key] = entry;

        offset += sizeof(entry);
    }

    for (auto& [key, val] : latest) {

        val.prevOffset = -1;
        write(newFd, &val, sizeof(LogEntry));
    }

    close(oldFd);
    close(newFd);

    remove(logFilename.c_str());
    rename("posts_new.log", logFilename.c_str());
}

std::vector<uint64_t> Indexer::search(const std::string& query)
{
    auto words = tokenize(query);

    if (words.empty())
        return {};

    int hashFd = open(hashFilename.c_str(), O_RDONLY);
    int logFd  = open(logFilename.c_str(), O_RDONLY);

    if (hashFd < 0 || logFd < 0) {
        std::cerr << "Error opening files.\n";
        return {};
    }

    auto getList = [&](const std::string& word) {

        std::vector<uint64_t> res;

        uint64_t idx = hashFunction(word);

        lseek(hashFd, idx * sizeof(Bucket), SEEK_SET);

        Bucket b;
        read(hashFd, &b, sizeof(Bucket));

        int64_t offset = b.offset;

        while (offset != -1) {

            lseek(logFd, offset, SEEK_SET);

            LogEntry entry;
            read(logFd, &entry, sizeof(entry));

            if (std::string(entry.word) == word) {
                res.push_back(entry.linkID);
            }

            offset = entry.prevOffset;
        }

        return res;
    };

    auto firstList = getList(words[0]);

    std::unordered_set<uint64_t> result(firstList.begin(), firstList.end());
    std::unordered_set<uint64_t> temp;

    for (size_t i = 1; i < words.size(); i++) {

        auto list = getList(words[i]);

        temp.clear();

        for (auto id : list) {
            if (result.count(id)) {
                temp.insert(id);
            }
        }

        result = temp;

        if (result.empty())
            break;
    }

    close(hashFd);
    close(logFd);

    return std::vector<uint64_t>(result.begin(), result.end());
}
