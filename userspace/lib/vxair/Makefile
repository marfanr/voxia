ROOT ?= $(shell pwd)/../../..

include $(ROOT)/scripts/vars.mk

CC = clang
TARGET = x86_64-linux-musl

CFLAGS = -g --config ./voxia.cfg -I./include -I./src

TARGET_BIN = libvxair.so
SRC = $(shell find src -name '*.c')

all: $(TARGET_BIN)
	@mkdir -p $(ISO_DIR)/lib/vxair/include
	cp $(TARGET_BIN) $(ISO_DIR)/lib/vxair/
	cp -r include/* $(ISO_DIR)/lib/vxair/include/

$(TARGET_BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET_BIN)

clean:
	rm -f $(TARGET_BIN)