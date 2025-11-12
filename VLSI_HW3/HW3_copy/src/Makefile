###############################################################################
# Wu & Chu Double-Row Detailed Placement
###############################################################################

CXX := g++
CXXFLAGS := -std=c++14 -O2 -Wall \
    -I../include \
    -I../lefdef/lef/include \
    -I../lefdef/def/include

LDFLAGS := \
    ../lefdef/lef/lib/liblef.a \
    ../lefdef/def/lib/libdef.a \
    -lm -lz

SRC_DIR := .
BIN_DIR := ../bin
OUT_DIR := ../output

TARGET := $(BIN_DIR)/hw3

SRCS := \
    $(SRC_DIR)/main.cpp \
    $(SRC_DIR)/pairing.cpp \
    $(SRC_DIR)/dp_stage.cpp \
    $(SRC_DIR)/io_def.cpp \
    $(SRC_DIR)/io_lef.cpp \


OBJS := $(SRCS:%.cpp=%.o)

all: prep $(TARGET)

prep:
	mkdir -p $(BIN_DIR) $(OUT_DIR)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: all
	$(TARGET) ../testcase/public1.lef ../testcase/public1.def ../output/public1.def

clean:
	rm -f *.o $(TARGET)
.PHONY: all prep run clean