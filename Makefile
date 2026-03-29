CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread

LIBS = -lcurl -lgumbo -lstemmer

SRC = main.cpp \
      crawler/pageLoader.cpp \
      crawler/htmlParser.cpp \
      indexer/indexer.cpp

OBJ = $(SRC:.cpp=.o)

TARGET = search_engine

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $(TARGET) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all run clean
