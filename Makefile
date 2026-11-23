# Makefile for PagePro 1390MF CUPS driver
# Builds the raster filter and test binary.

UNAME_S := $(shell uname -s)

# Detect platform and set compiler/arch flags
ifeq ($(UNAME_S),Darwin)
    CC := clang
    ARCH_FLAGS := -arch arm64 -arch x86_64
else
    CC := cc
    ARCH_FLAGS :=
endif

CFLAGS := -O2 -Wall -Wextra $(ARCH_FLAGS) -Isrc -Isrc/libjbig
LDFLAGS := $(ARCH_FLAGS)

BUILD_DIR := build
SRC_DIR := src
TEST_DIR := tests

# libjbig sources (vendored)
LIBJBIG_SRC := $(SRC_DIR)/libjbig/jbig.c $(SRC_DIR)/libjbig/jbig_ar.c

# Main filter objects
FILTER_OBJ := $(BUILD_DIR)/rastertopagepro1390.o \
              $(BUILD_DIR)/jbig1390.o \
              $(BUILD_DIR)/jbig.o \
              $(BUILD_DIR)/jbig_ar.o

# Test objects
TEST_OBJ := $(BUILD_DIR)/test_jbig1390.o \
            $(BUILD_DIR)/jbig1390.o \
            $(BUILD_DIR)/jbig.o \
            $(BUILD_DIR)/jbig_ar.o

.PHONY: all test clean

all: $(BUILD_DIR)/rastertopagepro1390

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# --- Filter ---
$(BUILD_DIR)/rastertopagepro1390: $(FILTER_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/rastertopagepro1390.o: $(SRC_DIR)/rastertopagepro1390.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/jbig1390.o: $(SRC_DIR)/jbig1390.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/jbig.o: $(SRC_DIR)/libjbig/jbig.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/jbig_ar.o: $(SRC_DIR)/libjbig/jbig_ar.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Test ---
test: $(BUILD_DIR)/test_jbig1390
	./$(BUILD_DIR)/test_jbig1390 $(TEST_DIR)/square.pbm /tmp/out_printer.jbg /tmp/out_decodable.jbg
	cd backend && python3 -m unittest test_pagepro1390_backend.py

$(BUILD_DIR)/test_jbig1390: $(TEST_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/test_jbig1390.o: $(TEST_DIR)/test_jbig1390.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
