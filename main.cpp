#include <iostream>
#include <fstream>
#include <queue>
#include <unordered_set>

#include "crawler/pageLoader.h"
#include "crawler/htmlParser.h"
#include "indexer/indexer.h"

int main() {
    std::ifstream file("urls.txt");
    std::queue<std::string> urlQueue;
    std::unordered_set<std::string> visited;
    Indexer indexer;

    std::string url;
    while (file >> url) {
        urlQueue.push(url);
    }

    int maxPages = 20;

    while (!urlQueue.empty() && visited.size() < maxPages) {
        std::string currentUrl = urlQueue.front();
        urlQueue.pop();

        if (visited.count(currentUrl)) continue;
        visited.insert(currentUrl);

        std::cout << "Crawling: " << currentUrl << std::endl;

        std::string html = PageLoader::downloadPage(currentUrl);
       /* if (html.empty()) {
            std::cout << "Failed to download\n";
            continue;
        }*/

        HtmlParser parser;
        std::string text = parser.extractText(html);
        if (text.empty()) continue;

        indexer.addDocument(currentUrl, text);

        auto links = parser.extractLinks(html, currentUrl);
        for (auto& link : links) {
            if (!visited.count(link)) {
                urlQueue.push(link);
            }
        }
    }
    while(true) {
    	 std::string query;
  	 std::cout << "\nEnter search query: ";
  	 std::getline(std::cin, query);

	 if(query == "exit") break;

	 auto results = indexer.search(query);

	 if(results.empty()) {
	     	std::cout << "\nThere are no results :(\n";
	 } else {
		std::cout << "\nResults:\n";
	 }

	 for (auto& r : results) {
       		std::cout << r << std::endl;
   	 }
    }
    return 0;
}
