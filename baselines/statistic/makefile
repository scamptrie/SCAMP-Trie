CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2

BUILD_DIR = build
TARGET = $(BUILD_DIR)/statistic
SOURCES = main.cpp fib_parser.cpp trie.cpp statistic.cpp compress_trie.cpp
OBJECTS = $(SOURCES:%.cpp=$(BUILD_DIR)/%.o)

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(BUILD_DIR) $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS)

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) res_inv.csv res_count.csv res_simi.csv res_compress.csv res_bitmap_address_bits.csv

.PHONY: all clean run
