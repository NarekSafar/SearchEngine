#include <iostream>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <regex>
#include <mutex>
#include <vector>
#include <chrono>
#include <sys/stat.h>
#include <atomic>
#include <netdb.h>
#include <condition_variable>
#include <fstream>
#include <cctype>

#include "argparse/argparse.hpp"
#include "fetcher/pageLoader.h"
#include "fetcher/htmlParser.h"
#include "indexer/indexer.h"

std::queue<std::string> urlQueue;
std::unordered_set<std::string> visited;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> inProgress;

std::mutex queueMutex;
std::mutex indexMutex;

std::unordered_map<std::string, uint64_t> urlToId;
std::vector<std::string> idToUrl;
std::mutex urlMapMutex;
uint64_t nextId = 0;

std::atomic<bool> compacting(false);
std::atomic<bool> stopWorkers(false);

std::atomic<int> crawlCount(0);
std::atomic<int> finishedCount(0);

std::atomic<int> consecutiveErrors(0);

std::mutex coutMutex;

uint64_t getUrlId(const std::string& url) {
    std::lock_guard<std::mutex> lock(urlMapMutex);

    auto it = urlToId.find(url);
    if (it != urlToId.end())
        return it->second;

    uint64_t id = nextId++;
    urlToId[url] = id;
    idToUrl.push_back(url);

    return id;
}

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(s[start])) start++;

    size_t end = s.size();
    while (end > start && std::isspace(s[end - 1])) end--;

    return s.substr(start, end - start);
}

void saveUrlMap(const std::string& fname)
{
    std::lock_guard<std::mutex> lock(urlMapMutex);

    std::ofstream out(fname + ".dat", std::ios::trunc);
    if (!out.is_open())
        return;

    for (uint64_t i = 0; i < idToUrl.size(); i++)
        if (!idToUrl[i].empty())
            out << i << "\n" << idToUrl[i] << "\n";
}

void worker(Indexer& indexer, int maxPages) {

    HtmlParser parser;
    bool infinite = (maxPages == 0);

    while (!stopWorkers.load()) {

        std::string currentUrl;

        {
            std::unique_lock<std::mutex> lock(queueMutex);

            if (urlQueue.empty()) {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            currentUrl = urlQueue.front();
            urlQueue.pop();

            if (visited.count(currentUrl))
                continue;

            visited.insert(currentUrl);
            inProgress[currentUrl] = std::chrono::steady_clock::now();
        }

        if (!infinite && crawlCount.load() >= maxPages) {
            stopWorkers.store(true);
            return;
        }

        uint64_t urlId = getUrlId(currentUrl);
        crawlCount.fetch_add(1);

        std::string html = PageLoader::downloadPage(currentUrl);

        if (html.empty()) {
            int err = consecutiveErrors.fetch_add(1) + 1;

            if (err >= 5) {
                std::lock_guard<std::mutex> lock(coutMutex);
                std::cout << "\n[ERROR] 5 consecutive download failures. Stopping crawler...\n";
                stopWorkers.store(true);
            }

            std::lock_guard<std::mutex> lock(queueMutex);
            inProgress.erase(currentUrl);
            continue;
        }

        consecutiveErrors.store(0);

        std::string text = parser.extractText(html);

        if (!text.empty()) {
            std::lock_guard<std::mutex> lock(indexMutex);
            if (!compacting.load()) {
                indexer.addDocument(urlId, text);
            }
        }

        auto links = parser.extractLinks(html, currentUrl);

        {
            std::lock_guard<std::mutex> lock(queueMutex);

            inProgress.erase(currentUrl);

            int done = finishedCount.fetch_add(1) + 1;

            {
                std::lock_guard<std::mutex> coutLock(coutMutex);
                std::cout << "[" << done << "] Crawled: " << currentUrl << "\n";
            }

            for (auto& link : links) {

                if (!infinite && crawlCount.load() >= maxPages)
                    break;

                if (!visited.count(link) && !inProgress.count(link)) {
                    urlQueue.push(link);
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {

    argparse::ArgumentParser program("./crawler");

    program.add_argument("-u", "--url")
        .required()
        .help("Base URL")
        .action([](const std::string& url) {
            std::regex pattern(R"(^https?:\/\/([a-zA-Z0-9-]+\.)+[a-zA-Z]{2,}(:\d+)?(\/.*)?$)");
            if (std::regex_match(url, pattern)) return url;
            throw std::runtime_error("Invalid URL");
        });

    program.add_argument("-m", "--max-pages")
        .required()
        .scan<'i', int>()
        .help("Max pages (0 = infinite)")
        .action([](const std::string& v) {
            int val = std::stoi(v);
            if (val < 0) throw std::runtime_error("must be >= 0");
            return val;
        });

    program.add_argument("-i", "--index")
        .required()
        .help("Index base name");

    program.parse_args(argc, argv);

    std::string url = program.get<std::string>("--url");
    int maxPages = program.get<int>("--max-pages");
    std::string base = program.get<std::string>("--index");

    std::cout << "Starting crawler...\n";

    Indexer indexer(base + ".bin", base + ".log");

    urlQueue.push(url);

    unsigned int cores = std::thread::hardware_concurrency();
    int workerCount = std::max(2u, cores * 2);

    std::vector<std::thread> workers;

    for (int i = 0; i < workerCount; i++) {
        workers.emplace_back(worker, std::ref(indexer), maxPages);
    }

    for (auto& t : workers)
        t.join();

    stopWorkers.store(true);

    saveUrlMap(base);

    return 0;
}
