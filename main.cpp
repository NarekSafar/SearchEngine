#include <iostream>
#include <fstream>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>

#include "crawler/pageLoader.h"
#include "crawler/htmlParser.h"
#include "indexer/indexer.h"

std::queue<std::string> urlQueue;
std::unordered_set<std::string> visited;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> inProgress;

std::mutex queueMutex;
std::mutex indexMutex;

std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(*start)) start++;

    auto end = s.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::isspace(*end));

    return std::string(start, end + 1);
}

const int TASK_TIMEOUT = 30;

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
    while (true) {
        std::string currentUrl;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            recoverTimedOutTasks();
            if (urlQueue.empty() || visited.size() >= maxPages) { return; }
            currentUrl = urlQueue.front();
            urlQueue.pop();
            if (visited.count(currentUrl)) { continue; }
            inProgress[currentUrl] = std::chrono::steady_clock::now();
        }
	
	std::cout << "Crawling: " << currentUrl << "\n"; 

        std::string html = PageLoader::downloadPage(currentUrl);
        if (html.empty()) {
            std::lock_guard<std::mutex> lock(queueMutex);
            urlQueue.push(currentUrl);
            inProgress.erase(currentUrl);
            continue;
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
                if (!visited.count(link)) { urlQueue.push(link); }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " urls.txt maxPages\n";
        return 1;
    }

    std::ifstream file(argv[1]);
    if (!file.is_open()) {
        std::cout << "Cannot open file: " << argv[1] << "\n";
        return 1;
    }

    std::string url;

    while (file >> url) {
        urlQueue.push(url);
    }

    int maxPages = std::stoi(argv[2]);
    if (maxPages <= 0) {
        std::cout << "Max pages count must be positive.\n";
        return 1;
    }

    unsigned int cores = std::thread::hardware_concurrency();
    int workerCount = std::min(maxPages, (int)std::max(1u, cores * 2));

    std::cout << "max pages: " << maxPages << ", workers: " << workerCount << "\n";

    Indexer indexer("inverted_index.bin");
    std::vector<std::thread> workers;

    for (int i = 0; i < workerCount; i++) {
        workers.emplace_back(worker, std::ref(indexer), maxPages);
    }

    for (auto& t : workers) {
        t.join();
    }

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
            for (auto& r : results) {
                std::cout << r << "\n";
            }
        }
    }
    return 0;
}
