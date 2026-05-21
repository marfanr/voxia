SRC_DIR     := src
BUILD_DIR   := build
INCLUDE_DIR := include

ROOT        ?= $(realpath ../..)
LIB_INCLUDE_DIR := $(ROOT)/include

MODULE_NAME := term
OUT_ELF     := $(MODULE_NAME).elf

CXX := clang++

TARGET := x86_64-unknown-none

CXXFLAGS := \
	--target=$(TARGET) \
	-ffreestanding \
	-fPIE \
	-fuse-ld=lld \
	-fno-exceptions \
	-fno-rtti \
	-std=c++20 \
	-O0 -ggdb \
	-I$(LIB_INCLUDE_DIR) \
	-I$(INCLUDE_DIR)

LDFLAGS := \
	--target=$(TARGET) \
	-pie \
	-Wl,-emain \
	-Wl,--no-dynamic-linker \
	-Wl,--build-id=none \
	-nostdlib

SRCS := $(shell find $(SRC_DIR) -type f -name '*.cpp')
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(OUT_ELF)

$(OUT_ELF): $(OBJS)
	@mkdir -p ../../root/sbin
	$(CXX) $(OBJS) $(LDFLAGS) -o $@
	cp $@ ../../root/sbin/

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(OUT_ELF)