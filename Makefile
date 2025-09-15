CXX := g++
CXXFLAGS := -std=c++20 -Iinclude

SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build
BIN_DIR := bin

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

TARGET := $(BIN_DIR)/benchmark

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) -latomic -pthread

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

plot:
	python plot_benchmarks.py \
	--csv msq_balanced.csv --csv fcq_balanced.csv --csv scq_balanced.csv \
	--csv msq_enqueueheavy.csv --csv fcq_enqueueheavy.csv --csv scq_enqueueheavy.csv \
	--csv msq_dequeueheavy.csv --csv fcq_dequeueheavy.csv --csv scq_dequeueheavy.csv \
	--csv msq_pair.csv --csv fcq_pair.csv --csv scq_pair.csv \
	--out ./graphs
