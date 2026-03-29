#ifndef HTML_PARSER_H
#define HTML_PARSER_H

#include <string>
#include <vector>
#include <gumbo.h>

class HtmlParser {
public:
    HtmlParser();
    ~HtmlParser();

    std::string extractText(const std::string& html);
    std::vector<std::string> extractLinks(const std::string& html, const std::string& baseUrl);

private:
    void extractTextFromNode(GumboNode* node, std::string& output);
    std::string getDomain(const std::string& url); 
};

#endif
