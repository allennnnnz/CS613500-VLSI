
CXX = g++
# CXXFLAGS = -std=c++17 -O0 -g -Wall
CXXFLAGS = -std=c++17 -O3 -Wall -fopenmp 
LDFLAGS =

SRC_DIR  = .
OBJ_DIR  = ../object
BIN_DIR  = ../bin

TARGET  = hw3
# Full path to the final binary (placed in ../bin/hw3)
BIN     = $(BIN_DIR)/$(TARGET)

SRCS = $(wildcard $(SRC_DIR)/*.cc)
OBJS = $(patsubst $(SRC_DIR)/%.cc, $(OBJ_DIR)/%.o, $(SRCS))

all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Compile .cc -> .o
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cc
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

debug: CXXFLAGS += -Og -DDEBUG
debug: clean all

clean:
	rm -rf $(OBJ_DIR) $(BIN)

.PHONY: all clean debug
