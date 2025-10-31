CXX = g++
CXXFLAGS = -std=c++14 -O2 -Wall -I../include -I../lefdef/lef/include -I../lefdef/def/include
LDFLAGS = -L../lefdef/lef/lib -L../lefdef/def/lib -llef -ldef -lm -lz

TARGET = hw3
BIN_DIR = ../bin
OUTPUT_DIR = ../output
SRC_DIR = .

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BIN_DIR)/%.o)

all: $(BIN_DIR) $(OUTPUT_DIR) $(BIN_DIR)/$(TARGET)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

$(BIN_DIR)/$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: all
	$(BIN_DIR)/$(TARGET)

clean:
	rm -rf $(BIN_DIR)