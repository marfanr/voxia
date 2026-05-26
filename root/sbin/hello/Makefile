ROOT ?= $(shell pwd)/../../..

include $(ROOT)/scripts/vars.mk

CC = clang
TARGET = x86_64-linux-musl

CFLAGS = --config ./voxia.cfg

TARGET_BIN = hello.elf
SRC = src/main.c

all: $(TARGET_BIN)
	@mkdir -p $(ISO_DIR)/sbin/
	cp $(TARGET_BIN) $(ISO_DIR)/sbin/

$(TARGET_BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET_BIN)

clean:
	rm -f $(TARGET_BIN)