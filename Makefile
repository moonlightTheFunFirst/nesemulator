CC = x86_64-w64-mingw32-gcc
BUILD_DIR := build
TARGET := $(BUILD_DIR)/nesemu.exe
TEST_TARGET := $(BUILD_DIR)/nesemu_core_tests.exe

CFLAGS ?= -std=c11 -Wall -Wextra -O2
CPPFLAGS := -Iinclude
WIN_CPPFLAGS := -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN
WIN_LDFLAGS := -mwindows -municode
WIN_LIBS := -luser32 -lshell32 -lgdi32 -lwinmm

CORE_SRCS := src/core/nes.c src/core/cpu.c src/core/apu.c src/core/mapper/mapper.c src/core/mapper/mapper0.c src/core/mapper/mapper3.c src/core/mapper/mapper4.c src/core/mapper/mapper10.c src/core/mapper/mapper19.c src/core/mapper/mapper88.c
WIN_SRCS := src/platform/win32/main.c
TEST_SRCS := tests/test_core.c

.PHONY: all clean run test

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(CORE_SRCS) $(WIN_SRCS) include/nesemu.h src/core/nes_internal.h src/core/apu.h src/core/mapper/mapper.h src/core/mapper/mapper0.h src/core/mapper/mapper3.h src/core/mapper/mapper4.h src/core/mapper/mapper10.h src/core/mapper/mapper19.h src/core/mapper/mapper88.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(WIN_CPPFLAGS) $(CFLAGS) -o $@ $(CORE_SRCS) $(WIN_SRCS) $(WIN_LDFLAGS) $(WIN_LIBS)

$(TEST_TARGET): $(CORE_SRCS) $(TEST_SRCS) include/nesemu.h src/core/nes_internal.h src/core/apu.h src/core/mapper/mapper.h src/core/mapper/mapper0.h src/core/mapper/mapper3.h src/core/mapper/mapper4.h src/core/mapper/mapper10.h src/core/mapper/mapper19.h src/core/mapper/mapper88.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE_SRCS) $(TEST_SRCS)

test: $(TEST_TARGET)
	$(TEST_TARGET)

run: $(TARGET)
	$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
