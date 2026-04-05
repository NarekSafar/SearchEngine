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

    size_t domainStart = protocolEnd + 3;
    size_t domainEnd = url.find('/', domainStart);

    if (domainEnd == std::string::npos)
        return url.substr(0);

    return url.substr(0, domainEnd);
}

bool HtmlParser::isCrawlableLink(const std::string& url) {
    static const std::vector<std::string> badExtensions = {
        ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".svg",
        ".pdf", ".zip", ".rar", ".gz", ".tar",
        ".mp4", ".mp3", ".avi", ".mov", ".wmv",
        ".css", ".js", ".ico"
    };

    size_t lastSlash = url.find_last_of('/');
    size_t lastDot = url.find_last_of('.');

    if (lastDot == std::string::npos || lastDot < lastSlash)
        return true;

    std::string ext = url.substr(lastDot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    for (const auto& bad : badExtensions) {
        if (ext == bad)
            return false;
    }
    return true;
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

        if (link[0] == '#') continue;
        if (link.find("javascript:") == 0) continue;
        if (link.find("mailto:") == 0) continue;

        std::string fullLink;

        if (link.find("http") == 0) {
            if (link.compare(0, domain.size(), domain) == 0 &&
                (link.size() == domain.size() || link[domain.size()] == '/')) {
                fullLink = link;
            }
        }
        else if (link[0] == '/') {
            fullLink = domain + link;
        }

        if (!fullLink.empty() && isCrawlableLink(fullLink)) {
            links.push_back(fullLink);
        }
    }

    return links;
}
