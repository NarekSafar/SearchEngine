#include "pageLoader.h"
#include <curl/curl.h>
#include <iostream>

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* buffer) {
    size_t totalSize = size * nmemb;
    buffer->append((char*)contents, totalSize);
    return totalSize;
}

std::string PageLoader::downloadPage(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string result;

    if (!curl) {
        std::cerr << "CURL init failed for URL: " << url << std::endl;
        return "";
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "DistributedCrawler/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    char* final_url = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &final_url);

    if (res == CURLE_TOO_MANY_REDIRECTS) {
        std::cerr << "Too many redirects: " << url << std::endl;
        result.clear();
    }
    else if (res == CURLE_OPERATION_TIMEDOUT) {
        std::cerr << "Timeout: " << url << std::endl;
        result.clear();
    }
    else if (res != CURLE_OK) {
        std::cerr << "CURL error: " << curl_easy_strerror(res)
                  << " for URL: " << url << std::endl;
        result.clear();
    }
    else if (http_code >= 400) {
        std::cerr << "HTTP error " << http_code
                  << " for URL: " << url << std::endl;
        result.clear();
    }

    curl_easy_cleanup(curl);
    return result;
}
