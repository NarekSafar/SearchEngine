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

#include "argparse/argparse.hpp"
#include "crawler/pageLoader.h"
#include "crawler/htmlParser.h"
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

std::mutex coutMutex;

const uint64_t MAX_LOG_SIZE = 1ULL * 1024 * 1024 * 1024;

std::condition_variable cv;
std::mutex cvMutex;

uint64_t getFileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return st.st_size;
}

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

const int TASK_TIMEOUT = 30;

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(s[start])) start++;

    size_t end = s.size();
    while (end > start && std::isspace(s[end - 1])) end--;

    return s.substr(start, end - start);
}

bool urlExists(const std::string& url) {
    std::string host = url;

    if (host.find("http://") == 0)
        host = host.substr(7);
    else if (host.find("https://") == 0)
        host = host.substr(8);

    size_t slash = host.find('/');
    if (slash != std::string::npos)
        host = host.substr(0, slash);

    struct addrinfo hints{}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(host.c_str(), nullptr, &hints, &res);

    if (status != 0) return false;

    freeaddrinfo(res);
    return true;
}

void recoverTimedOutTasks() {
    auto now = std::chrono::steady_clock::now();

    for (auto it = inProgress.begin(); it != inProgress.end();) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();

        if (elapsed > TASK_TIMEOUT) {
            urlQueue.push(it->first);
            it = inProgress.erase(it);
        } else {
            ++it;
        }
    }
}

void loadUrlMap(const std::string& fname) {
    std::lock_guard<std::mutex> lock(urlMapMutex);

    std::ifstream in(fname + ".dat");
    if (!in.is_open()) return;

    urlToId.clear();
    idToUrl.clear();
    nextId = 0;

    uint64_t id;
    std::string url;

    while (in >> id) {
        in.ignore();
        std::getline(in, url);

        if (idToUrl.size() <= id)
            idToUrl.resize(id + 1);

        idToUrl[id] = url;
        urlToId[url] = id;

        if (id >= nextId)
            nextId = id + 1;
    }
}

void saveUrlMap(const std::string& fname) {
    std::lock_guard<std::mutex> lock(urlMapMutex);

    std::ofstream out(fname + ".dat", std::ios::trunc);
    if (!out.is_open()) return;

    for (uint64_t id = 0; id < idToUrl.size(); id++) {
        if (!idToUrl[id].empty())
            out << id << "\n" << idToUrl[id] << "\n";
    }
}

void worker(Indexer& indexer, int maxPages) {

    HtmlParser parser;
    static int consecutiveErrors = 0;
    static std::mutex errorMutex;

    while (true) {

        std::string currentUrl;

        {
            std::unique_lock<std::mutex> lock(queueMutex);

            recoverTimedOutTasks();

            if (stopWorkers.load())
                return;

            if (urlQueue.empty())
                continue;

            if (crawlCount.load() >= maxPages) {
                stopWorkers.store(true);
                return;
            }

            currentUrl = urlQueue.front();
            urlQueue.pop();

            if (visited.count(currentUrl) || inProgress.count(currentUrl))
                continue;

            inProgress[currentUrl] = std::chrono::steady_clock::now();
        }

        uint64_t urlId = getUrlId(currentUrl);

        int reserved = crawlCount.fetch_add(1);

        if (reserved >= maxPages) {
            stopWorkers.store(true);
            std::lock_guard<std::mutex> lock(queueMutex);
            inProgress.erase(currentUrl);
            return;
        }

        std::string html = PageLoader::downloadPage(currentUrl);

        if (html.empty()) {

            {
                std::lock_guard<std::mutex> lock(queueMutex);

                std::lock_guard<std::mutex> eLock(errorMutex);
                consecutiveErrors++;

                if (consecutiveErrors >= 5) {
                    stopWorkers.store(true);
                }

                if (!visited.count(currentUrl) && !inProgress.count(currentUrl)) {
                    urlQueue.push(currentUrl);
                }

                inProgress.erase(currentUrl);
            }

            continue;
        }
        else {
            std::lock_guard<std::mutex> eLock(errorMutex);
            consecutiveErrors = 0;
        }

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
            visited.insert(currentUrl);

            int printedId = finishedCount.fetch_add(1) + 1;

            {
                std::lock_guard<std::mutex> coutLock(coutMutex);
                std::cout << "[" << printedId << "] Crawled: " << currentUrl << "\n";
            }

            for (auto& link : links) {
                if (crawlCount.load() >= maxPages) break;

                if (!visited.count(link) && !inProgress.count(link))
                    urlQueue.push(link);
            }
        }
    }
}

int main(int argc, char* argv[]) {

    argparse::ArgumentParser program("./search_engine");

    program.add_argument("-u", "--url")
        .required()
        .help("Base URL")
        .action([](const std::string& url) {
            std::regex pattern(R"(^https?:\/\/([a-zA-Z0-9-]+\.)+[a-zA-Z]{2,}(:\d+)?(\/.*)?$)");
            if (std::regex_match(url, pattern) && urlExists(url)) return url;
            throw std::runtime_error("URL pattern is wrong or URL does not exist.");
        });

    program.add_argument("-m", "--max-pages")
        .required()
        .scan<'i', int>()
        .help("Maximum number of pages")
        .action([](const std::string& value) {
            int v = std::stoi(value);
            if (v <= 0) throw std::runtime_error("Max-pages must be positive.");
            return v;
        });

    program.add_argument("-i", "--index")
        .required()
        .help("Base name for index files (without extension)");

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << "\n";
        std::cerr << program;
        return 1;
    }

    std::string url = program.get<std::string>("--url");
    int maxPages = program.get<int>("--max-pages");
    std::string base = program.get<std::string>("--index");

    Indexer indexer(base + ".bin", base + ".log");

    loadUrlMap(base);

    urlQueue.push(url);

    unsigned int cores = std::thread::hardware_concurrency();
    int workerCount = std::min(maxPages, (int)std::max(1u, cores * 2));

    std::vector<std::thread> workers;

    for (int i = 0; i < workerCount; i++) {
        workers.emplace_back(worker, std::ref(indexer), maxPages);
    }

    std::thread compactionThread([&]() {

        std::unique_lock<std::mutex> lk(cvMutex);

        while (!stopWorkers.load()) {

            cv.wait_for(lk, std::chrono::seconds(10), [&]() {
                return stopWorkers.load();
            });

            if (stopWorkers.load())
                break;

            uint64_t size = getFileSize(base + ".log");

            if (size > MAX_LOG_SIZE && !compacting.load()) {

                std::cout << "\n[COMPACTION START]\n\n";

                compacting.store(true);

                {
                    std::lock_guard<std::mutex> lock(indexMutex);
                    indexer.compact();
                }

                compacting.store(false);

                std::cout << "\n[COMPACTION DONE]\n\n";
            }
        }
    });

    for (auto& t : workers)
        t.join();

    stopWorkers.store(true);
    cv.notify_all();

    compactionThread.join();

    saveUrlMap(base);

    while (true) {
        std::string query;
        std::cout << "\nEnter search query (or type exit): ";
        std::getline(std::cin, query);
        query = trim(query);

        if (query == "exit") break;
        if (query.empty()) continue;

        auto results = indexer.search(query);
        std::cout << "Results:\n";

        if (results.empty()) {
            std::cout << "No results found.\n";
        } else {
            for (uint64_t id : results) {
                if (id < idToUrl.size())
                    std::cout << idToUrl[id] << "\n";
            }
        }
    }

    return 0;
}
