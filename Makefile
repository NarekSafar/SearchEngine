CXX = g++

CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -MMD -MP -Icpp-httplib -Iargparse -I.

LIBS = -lcurl -lgumbo -lstemmer

CRAWLER_SRC = crawler.cpp \
              fetcher/pageLoader.cpp \
              fetcher/htmlParser.cpp \
              indexer/indexer.cpp

SEARCH_SRC = search_server.cpp \
             indexer/indexer.cpp

CRAWLER_OBJ = $(CRAWLER_SRC:.cpp=.o)
SEARCH_OBJ = $(SEARCH_SRC:.cpp=.o)

CRAWLER = crawler
SEARCH = search

all: $(CRAWLER) $(SEARCH)

$(CRAWLER): $(CRAWLER_OBJ)
	$(CXX) $(CRAWLER_OBJ) -o $(CRAWLER) $(LIBS)

$(SEARCH): $(SEARCH_OBJ)
	$(CXX) $(SEARCH_OBJ) -o $(SEARCH) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(CRAWLER_OBJ:.o=.d)
-include $(SEARCH_OBJ:.o=.d)

clean:
	find . -type f -name "*.o" -delete
	find . -type f -name "*.d" -delete
	rm -f $(CRAWLER) $(SEARCH)

.PHONY: all clean
