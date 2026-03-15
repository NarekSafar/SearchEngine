#include <iostream>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>

#include "argparse/argparse.hpp"

#include "crawler/pageLoader.h"
#include "crawler/htmlParser.h"
#include "indexer/indexer.h"

std::queue<std::string> urlQueue;
std::unordered_set<std::string> visited;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> inProgress;

std::mutex queueMutex;
std::mutex indexMutex;

const int TASK_TIMEOUT = 30;

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(s[start])) start++;

    size_t end = s.size();
    while (end > start && std::isspace(s[end - 1])) end--;

    return s.substr(start, end - start);
}

void recoverTimedOutTasks() {
    auto now = std::chrono::steady_clock::now();

    for (auto it = inProgress.begin(); it != inProgress.end();) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second
        ).count();

        if (elapsed > TASK_TIMEOUT) {
            urlQueue.push(it->first);
            it = inProgress.erase(it);
        } else {
            ++it;
        }
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

            if (urlQueue.empty() || visited.size() >= maxPages)
                return;

            currentUrl = urlQueue.front();
            urlQueue.pop();

            if (visited.count(currentUrl))
                continue;

            inProgress[currentUrl] = std::chrono::steady_clock::now();
        }

        std::cout << "Crawling: " << currentUrl << "\n";

        std::string html = PageLoader::downloadPage(currentUrl);

        if (html.empty()) {

            std::lock_guard<std::mutex> lock(queueMutex);

            {
                std::lock_guard<std::mutex> eLock(errorMutex);
                consecutiveErrors++;

                if (consecutiveErrors >= 5) {
                    std::cerr << "Too many consecutive errors. Stopping crawl.\n";
                    exit(1);
                }
            }

            urlQueue.push(currentUrl);
            inProgress.erase(currentUrl);

            continue;
        } else {
            std::lock_guard<std::mutex> eLock(errorMutex);
            consecutiveErrors = 0;
        }

        std::string text = parser.extractText(html);

        if (!text.empty()) {
            std::lock_guard<std::mutex> lock(indexMutex);
            indexer.addDocument(currentUrl, text);
        }

        auto links = parser.extractLinks(html, currentUrl);

        {
            std::lock_guard<std::mutex> lock(queueMutex);

            inProgress.erase(currentUrl);
            visited.insert(currentUrl);

            for (auto& link : links) {
                if (!visited.count(link))
                    urlQueue.push(link);
            }
        }
    }
}

int main(int argc, char* argv[]) {

    argparse::ArgumentParser program("Search engine");

    program.add_argument("-u", "--url")
        .required()
        .help("Base URL");

    program.add_argument("-m", "--max-pages")
    .required()
    .scan<'i', int>()
    .help("Maximum number of pages")
    .action([](const std::string& value) {
        int v = std::stoi(value);
        if (v <= 0)
            throw std::runtime_error("max-pages must be positive");
        return v;
    });

    program.add_argument("-i", "--index")
        .required()
        .help("Index file");

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err) {
        std::cerr << err.what() << "\n";
        std::cerr << program;
        return 1;
    }

    std::string url = program.get<std::string>("--url");
    int maxPages = program.get<int>("--max-pages");
    std::string indexFile = program.get<std::string>("--index");

    urlQueue.push(url);

    unsigned int cores = std::thread::hardware_concurrency();
    int workerCount = std::min(maxPages, (int)std::max(1u, cores * 2));

    Indexer indexer(indexFile);

    std::vector<std::thread> workers;

    for (int i = 0; i < workerCount; i++) {
        workers.emplace_back(worker, std::ref(indexer), maxPages);
    }

    for (auto& t : workers)
        t.join();

    while (true) {
        std::string query;
        std::cout << "\nEnter search query (or type exit): ";
        std::getline(std::cin, query);
        query = trim(query);
        if (query == "exit") break;
        if (query.empty()) continue;
        auto results = indexer.search(query);

        if (results.empty()) {
            std::cout << "No results found.\n";
        } else {
            std::cout << "Results:\n";
            for (auto& r : results) std::cout << r << "\n";
        }
    }
    return 0;
}
