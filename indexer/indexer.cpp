#include "indexer.h"
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

Indexer::Indexer(const std::string& fname) : filename(fname) {
    stemmer = sb_stemmer_new("english", NULL);	
    if(stemmer == nullptr) {
    	std::cerr << "Error: Could not initialize libstemmer for English.\n"; 
    }	
    int fd = open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
    if (fd >= 0) {
        Bucket emptyBucket = {0};
        for (uint64_t i = 0; i < BUCKET_COUNT; ++i) {
            write(fd, &emptyBucket, sizeof(Bucket));
        }
        close(fd);
    // else {
    // 	std::cerr << "Error: File is not opened.\n";
	//return;
     //}
   }
}

Indexer::~Indexer() {
    if (stemmer) {
    	sb_stemmer_delete(stemmer);
	stemmer = nullptr;
    } 
}

uint64_t Indexer::hashFunction(const std::string& key) {
    uint64_t hash = 5381; // djb2 hash algorithm
    for (char c : key) hash = ((hash << 5) + hash) + c; 
    return hash % BUCKET_COUNT;
}

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
        if (word.empty() || stopWords.count(word)) continue;

        if (stemmer) {		
            const sb_symbol* stemmed = sb_stemmer_stem(
                stemmer,
                reinterpret_cast<const sb_symbol*>(word.c_str()),
                word.length()
            );
            if (stemmed) {      
                tokens.push_back(reinterpret_cast<const char*>(stemmed));
            }
        } else {
            tokens.push_back(word);
        }
    }
    return tokens;
}

void Indexer::addDocument(const std::string& url, const std::string& text) {
    auto words = tokenize(text);
    std::unordered_set<std::string> uniqueWords(words.begin(), words.end());

    int fd = open(filename.c_str(), O_RDWR);
    if (fd < 0) {
    	std::cerr << "Error: File could not be opened.\n";
	return;
    }

    for (const auto& word : uniqueWords) {
        uint64_t bucketIdx = hashFunction(word);
        uint64_t bucketPos = bucketIdx * sizeof(Bucket);
        
        Bucket b;
        lseek(fd, bucketPos, SEEK_SET);
        read(fd, &b, sizeof(Bucket));

        uint64_t currentOffset = b.offset;
        uint64_t prevPointerPos = bucketPos; 
        bool isBucketLink = true;
        bool found = false;

        while (currentOffset != 0) {
            lseek(fd, currentOffset, SEEK_SET);
            NodeHeader head;
            read(fd, &head, sizeof(NodeHeader));

            std::string storedWord(head.wordLen, ' ');
            read(fd, &storedWord[0], head.wordLen);

            if (storedWord == word) {
                std::vector<std::string> urls;
                for(uint64_t i=0; i < head.numUrls; ++i) {
                    uint64_t uLen;
                    read(fd, &uLen, sizeof(uLen));
                    std::string u(uLen, ' ');
                    read(fd, &u[0], uLen);
                    urls.push_back(u);
                }

                if (std::find(urls.begin(), urls.end(), url) == urls.end()) {
                    urls.push_back(url);
                    
                    uint64_t newNodeOffset = lseek(fd, 0, SEEK_END);
                    NodeHeader newHead = { (uint64_t)word.size(), (uint64_t)urls.size(), head.nextNodeOffset };
                    
                    write(fd, &newHead, sizeof(NodeHeader));
                    write(fd, word.c_str(), word.size());
                    for(const auto& u : urls) {
                        uint64_t uLen = u.size();
                        write(fd, &uLen, sizeof(uLen));
                        write(fd, u.c_str(), uLen);
                    }

                    lseek(fd, isBucketLink ?
				    prevPointerPos 
				    : (prevPointerPos + offsetof(NodeHeader, nextNodeOffset)), SEEK_SET);
                    write(fd, &newNodeOffset, sizeof(uint64_t));
                }
                found = true;
                break;
            }
            isBucketLink = false;
            prevPointerPos = currentOffset;
            currentOffset = head.nextNodeOffset;
        }

        if (!found) {
            uint64_t newNodeOffset = lseek(fd, 0, SEEK_END);
            NodeHeader newHead = { (uint64_t)word.size(), 1, 0 };
            
            write(fd, &newHead, sizeof(NodeHeader));
            write(fd, word.c_str(), word.size());
            uint64_t uLen = url.size();
            write(fd, &uLen, sizeof(uLen));
            write(fd, url.c_str(), uLen);

            lseek(fd, isBucketLink ? prevPointerPos : (prevPointerPos + offsetof(NodeHeader, nextNodeOffset)), SEEK_SET);
            write(fd, &newNodeOffset, sizeof(uint64_t));
        }
    }
    close(fd);
}

std::vector<std::string> Indexer::search(const std::string& query) {
    auto qWords = tokenize(query);
    if (qWords.empty()) return {};

    int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0) {
    	std::cerr << "Error: File could not be opened.\n";
	return {};
    }

    auto getUrls = [&](const std::string& word) -> std::vector<std::string> {
        uint64_t bucketIdx = hashFunction(word);
        lseek(fd, bucketIdx * sizeof(Bucket), SEEK_SET);
        Bucket b;
        read(fd, &b, sizeof(Bucket));

        uint64_t currentOffset = b.offset;
        while (currentOffset != 0) {
            lseek(fd, currentOffset, SEEK_SET);
            NodeHeader head;
            read(fd, &head, sizeof(NodeHeader));

            std::string storedWord(head.wordLen, ' ');
            read(fd, &storedWord[0], head.wordLen);

            if (storedWord == word) {
                std::vector<std::string> urls;
                for (uint64_t i = 0; i < head.numUrls; ++i) {
                    uint64_t uLen;
                    read(fd, &uLen, sizeof(uLen));
                    std::string u(uLen, ' ');
                    read(fd, &u[0], uLen);
                    urls.push_back(u);
                }
                return urls;
            }
            currentOffset = head.nextNodeOffset;
        }
        return {};
    };

    std::vector<std::vector<std::string>> lists;

    for (const auto& word : qWords) {
        auto urls = getUrls(word);
        if (urls.empty()) {
            close(fd);
            return {};
        }
        lists.push_back(std::move(urls));
    }

    std::sort(lists.begin(), lists.end(),
        [](const auto& a, const auto& b) {
            return a.size() < b.size();
        });

    std::unordered_set<std::string> resultSet(
        lists[0].begin(), lists[0].end());

    for (size_t i = 1; i < lists.size(); ++i) {

        std::unordered_set<std::string> nextSet(
            lists[i].begin(), lists[i].end());

        std::unordered_set<std::string> newSet;

        for (const auto& url : resultSet) {
            if (nextSet.count(url)) {
                newSet.insert(url);
            }
        }
        resultSet = std::move(newSet);

        if (resultSet.empty()) break;
    }

    close(fd);

    return std::vector<std::string>(
        resultSet.begin(),
        resultSet.end()
    );
}
