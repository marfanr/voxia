SRC_DIR     := src
BUILD_DIR   := build
INCLUDE_DIR := ./include
ROOT        ?= $(realpath ../..)

LIB_INCLUDE_DIR := $(ROOT)/include

MODULE_NAME := ahci
OUT_ELF     := $(MODULE_NAME).elf
OUT_VOXMO   := ../../initrd/modules/$(MODULE_NAME).voxmo

CXX := clang++
LD  := ld


WARNING := -Wall -Wextra -Werror -Wshadow -Wpointer-arith -Wcast-align \
	-Wstrict-prototypes -Wmissing-prototypes -Wundef -Wconversion \
	-Wdouble-promotion -Wvla -Wstrict-aliasing=2 -Warray-bounds \
	-Wformat=2 -Wuninitialized

CXXFLAGS := $(WARNING) -Wall -Wextra -O2 -pipe \
    -ffreestanding -nostdlib -fno-exceptions -fno-rtti -fno-builtin \
    -fno-use-cxa-atexit -fno-asynchronous-unwind-tables -fno-common \
    -mno-red-zone -std=c++20 -ggdb -g -fPIC \
    -I$(LIB_INCLUDE_DIR) -I$(INCLUDE_DIR)

LDFLAGS := -shared -m elf_x86_64 -e module_init

# cari semua file cpp secara rekursif
SRCS := $(shell find $(SRC_DIR) -type f -name '*.cpp')
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(OUT_ELF)

$(OUT_ELF): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	voxmo -o $(OUT_VOXMO) $(OUT_ELF) ./manifest.yaml

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(OUT_ELF) $(OUT_VOXMO)
