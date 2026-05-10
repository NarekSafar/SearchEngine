#ifndef PAGE_LOADER_H
#define PAGE_LOADER_H

#include <string>

class PageLoader {
public:
    static std::string downloadPage(const std::string& url);
};

#endif
