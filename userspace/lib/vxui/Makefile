ROOT ?= $(shell pwd)/../../..

include $(ROOT)/scripts/vars.mk

CC = clang
TARGET = x86_64-linux-musl

CFLAGS = --config ./voxia.cfg

TARGET_BIN = libvxui.so
SRC = $(shell find src -name '*.c')

all: $(TARGET_BIN)
	@mkdir -p $(ISO_DIR)/lib/vxui/include
	cp $(TARGET_BIN) $(ISO_DIR)/lib/vxui/
	cp -r include/* $(ISO_DIR)/lib/vxui/include/

$(TARGET_BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET_BIN)

clean:
	rm -f $(TARGET_BIN)