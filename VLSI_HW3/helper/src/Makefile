###############################################################################
# HW3 Makefile (revised)
# 需求：
#   - 物件檔 (*.o) 放在 src/ 這邊
#   - 最終執行檔 hw3 放到 ../bin/hw3 (bin 只有它)
#   - 用 g++-9 / gcc-9
#   - 連結你已經準備好的靜態 lib (liblef.a, libdef.a)
###############################################################################

CC      := gcc
CXX     := g++

CXXFLAGS := -std=c++14 -O2 -Wall \
    -I../include \



# 路徑設定
SRC_DIR     := .
BIN_DIR     := ../bin
OUTPUT_DIR  := ../output

# 最終執行檔 (放在 ../bin/hw3)
TARGET := $(BIN_DIR)/hw3

# 專案的 .cpp 檔
SRCS := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/*.cc)

# 物件檔要在 src 這邊，不要丟到 bin
OBJS := $(SRCS:%.cpp=%.o)

###############################################################################
# Top rules
###############################################################################

# 預設：產生 ../bin/hw3
all: prep $(TARGET)

# 確保 bin/ 跟 output/ 存在
prep:
	mkdir -p $(BIN_DIR) $(OUTPUT_DIR)

# 連結出 ../bin/hw3
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# 編譯每個 .cpp 成同目錄下的 .o
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

###############################################################################
# helper targets
###############################################################################

# 用 public1 當例子跑一次
run: all
	$(TARGET) ../testcase/public1.lef ../testcase/public1.def ../output/public1.def

# 清理：刪 .o 以及 bin/hw3
clean:
	rm -f *.o
	rm -f $(TARGET)

.PHONY: all prep run clean
