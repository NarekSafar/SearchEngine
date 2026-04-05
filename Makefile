CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -MMD -MP

LIBS = -lcurl -lgumbo -lstemmer

SRC = main.cpp \
      crawler/pageLoader.cpp \
      crawler/htmlParser.cpp \
      indexer/indexer.cpp

OBJ = $(SRC:.cpp=.o)
DEP = $(OBJ:.o=.d)

TARGET = search_engine

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $(TARGET) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(DEP)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(DEP) $(TARGET)

.PHONY: all run clean
