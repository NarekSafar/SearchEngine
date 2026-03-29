#include "htmlParser.h"
#include <regex>

HtmlParser::HtmlParser() {}
HtmlParser::~HtmlParser() {}

void HtmlParser::extractTextFromNode(GumboNode* node, std::string& output) {
    if (!node) return;

    if (node->type == GUMBO_NODE_ELEMENT) {
        GumboTag tag = node->v.element.tag;

        if (tag == GUMBO_TAG_SCRIPT ||
            tag == GUMBO_TAG_STYLE  ||
            tag == GUMBO_TAG_NAV    ||
            tag == GUMBO_TAG_FOOTER ||
            tag == GUMBO_TAG_HEADER ||
            tag == GUMBO_TAG_NOSCRIPT ||
            tag == GUMBO_TAG_HEAD   ||
            tag == GUMBO_TAG_META) {
            return;
        }
    }

    if (node->type == GUMBO_NODE_TEXT) {
        output.append(node->v.text.text);
        output.append(" ");
    }

    if (node->type == GUMBO_NODE_ELEMENT) {
        GumboVector* children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; i++) {
            extractTextFromNode(static_cast<GumboNode*>(children->data[i]), output);
        }
    }
}

std::string HtmlParser::extractText(const std::string& html) {
    GumboOutput* gumboOutput = gumbo_parse(html.c_str());
    std::string result;
    extractTextFromNode(gumboOutput->root, result);
    
    return result;
}

std::string HtmlParser::getDomain(const std::string& url) {
    size_t protocolEnd = url.find("://");
    if (protocolEnd == std::string::npos) return "";

    size_t domainEnd = url.find('/', protocolEnd + 3);

    if (domainEnd == std::string::npos)
        return url;

    return url.substr(0, domainEnd);
}

std::vector<std::string> HtmlParser::extractLinks(const std::string& html, const std::string& baseUrl) {
    std::vector<std::string> links;
    std::regex linkRegex("<a\\s+[^>]*href=\"([^\"]+)\"");
    auto begin = std::sregex_iterator(html.begin(), html.end(), linkRegex);
    auto end = std::sregex_iterator();
    std::string domain = getDomain(baseUrl);

    for (auto it = begin; it != end; ++it) {
        std::string link = (*it)[1];

        if (link.empty()) continue;

        if (link.find("http") == 0) {
            if (link.find(domain) == 0) {
                links.push_back(link);
            }
        }
        else if (link[0] == '/') {
            links.push_back(domain + link);
        }
    }
    return links;
}
