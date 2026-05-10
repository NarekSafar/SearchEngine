#include <iostream>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <string>
#include <cctype>
#include <cmath>

#include "cpp-httplib/httplib.h"
#include "argparse/argparse.hpp"
#include "indexer/indexer.h"

constexpr int PORT = 8080;

std::unordered_map<uint64_t, std::string> idToUrl;

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(s[start])) start++;

    size_t end = s.size();
    while (end > start && std::isspace(s[end - 1])) end--;

    return s.substr(start, end - start);
}

std::string escapeJson(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

void loadUrlMap(const std::string& fname) {
    std::ifstream in(fname + ".dat");
    if (!in.is_open()) {
        std::cerr << "Warning: Could not open " << fname << ".dat\n";
        return;
    }

    uint64_t id;
    std::string url;

    while (in >> id) {
        in.ignore();
        std::getline(in, url);
        idToUrl[id] = url;
    }
}

int main(int argc, char* argv[]) {
    argparse::ArgumentParser program("./search");

    program.add_argument("-i", "--index").required();

    try {
        program.parse_args(argc, argv);
    } catch (...) {
        std::cerr << program;
        return 1;
    }

    std::string base = program.get<std::string>("--index");

    std::cout << "=====================================\n";
    std::cout << "Starting Search Server\n";
    std::cout << "Index base: " << base << "\n";
    std::cout << "=====================================\n";

    std::cout << "Loading index...\n";
    Indexer indexer(base + ".bin", base + ".log");

    std::cout << "Loading URL map...\n";
    loadUrlMap(base);

    std::cout << "Loaded " << idToUrl.size() << " URLs\n";

    httplib::Server svr;

    svr.Get("/search", [&](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");

        if (!req.has_param("q")) {
            std::cout << "Missing query parameter\n";
            res.set_content("{\"error\":\"missing query\"}", "application/json");
            return;
        }

        std::string query = trim(req.get_param_value("q"));

        int page = 1;
        int limit = 10;
        std::string mode = "any";

        if (req.has_param("page")) {
            page = std::stoi(req.get_param_value("page"));
        }

        if (req.has_param("limit")) {
            limit = std::stoi(req.get_param_value("limit"));
        }

        if (req.has_param("mode")) {
            mode = req.get_param_value("mode");
        }

        std::cout << "-----------------------------\n";
        std::cout << "Query: " << query << "\n";
        std::cout << "Page: " << page << " | Limit: " << limit << " | Mode: " << mode << "\n";

        auto results = indexer.search(query, mode);

        int total = results.size();
        int start = (page - 1) * limit;
        int end = std::min(start + limit, total);

        if (start >= total) {
            start = end = 0;
        }

        std::cout << "Results found: " << total << "\n";
        std::cout << "Showing: " << start << " to " << end << "\n";

        std::string json = "{";
        json += "\"total\":" + std::to_string(total) + ",";
        json += "\"page\":" + std::to_string(page) + ",";
        json += "\"limit\":" + std::to_string(limit) + ",";
        json += "\"results\":[";

        bool first = true;

        for (int i = start; i < end; ++i) {
            auto& r = results[i];

            auto it = idToUrl.find(r.linkID);
            if (it == idToUrl.end()) continue;

            if (!first) json += ",";
            first = false;

            json += "{";
            json += "\"id\":" + std::to_string(r.linkID) + ",";
            json += "\"url\":\"" + escapeJson(it->second) + "\",";
            json += "\"score\":" + std::to_string(r.score) + ",";
            json += "\"missing\":[";

            bool firstWord = true;
            for (const auto& w : r.missingWords) {
                if (!firstWord) json += ",";
                firstWord = false;

                json += "\"";
                json += escapeJson(w);
                json += "\"";
            }

            json += "]}";
        }

        json += "]}";

        res.set_content(json, "application/json");
    });

    std::cout << "=====================================\n";
    std::cout << "Server is running\n";
    std::cout << "Open: http://localhost:" << PORT << "/search?q=test&page=1&limit=10&mode=any\n";
    std::cout << "=====================================\n";

    if (!svr.listen("0.0.0.0", PORT)) {
        std::cerr << "Failed to start server\n";
        return 1;
    }

    return 0;
}
