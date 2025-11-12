# Top-level Makefile for HW3
# Builds binary to bin/hw3 and provides run/verify helpers

.PHONY: all build run verify clean

# Paths
SRC_DIR := src
BIN_DIR := bin
TEST_LEF := testcase/public1.lef
TEST_DEF := testcase/public1.def
OUT_DEF  := output/public1.out
VERIFIER := verifier/verify

all: build

build:
	$(MAKE) -C $(SRC_DIR) all

run: build
	./$(BIN_DIR)/hw3 $(TEST_LEF) $(TEST_DEF) $(OUT_DEF)

verify: run
	./$(VERIFIER) $(TEST_LEF) $(TEST_DEF) $(OUT_DEF)

clean:
	$(MAKE) -C $(SRC_DIR) clean
	@echo "Cleaned build artifacts"
