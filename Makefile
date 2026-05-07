CC = x86_64-w64-mingw32-gcc
BUILD_DIR := build
TARGET := $(BUILD_DIR)/nesemu.exe
TEST_TARGET := $(BUILD_DIR)/nesemu_core_tests.exe

CFLAGS ?= -std=c11 -Wall -Wextra -O2
CPPFLAGS := -Iinclude
WIN_CPPFLAGS := -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN
WIN_LDFLAGS := -mwindows -municode
WIN_LIBS := -luser32 -lshell32 -lgdi32 -lwinmm

CORE_SRCS := src/core/nes.c src/core/cpu.c
WIN_SRCS := src/platform/win32/main.c
TEST_SRCS := tests/test_core.c

.PHONY: all clean run test

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(CORE_SRCS) $(WIN_SRCS) include/nesemu.h src/core/nes_internal.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(WIN_CPPFLAGS) $(CFLAGS) -o $@ $(CORE_SRCS) $(WIN_SRCS) $(WIN_LDFLAGS) $(WIN_LIBS)

$(TEST_TARGET): $(CORE_SRCS) $(TEST_SRCS) include/nesemu.h src/core/nes_internal.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE_SRCS) $(TEST_SRCS)

test: $(TEST_TARGET)
	$(TEST_TARGET)

run: $(TARGET)
	$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
