ROOT ?= $(shell pwd)/../../..

include $(ROOT)/scripts/vars.mk

CC = clang
TARGET = x86_64-linux-musl

CFLAGS = --config ./voxia.cfg

TARGET_BIN = toolbox
SRC = src/main.c \
	src/mount.c \
	src/ls.c

all: $(TARGET_BIN)
	@mkdir -p $(ISO_DIR)/sbin/
	ln -s $(TARGET_BIN) mount
	ln -s $(TARGET_BIN) umount
	ln -s $(TARGET_BIN) ls
	mv $(TARGET_BIN) $(ISO_DIR)/sbin/
	mv mount umount ls $(ISO_DIR)/sbin/

$(TARGET_BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET_BIN)

clean:
	rm -f $(TARGET_BIN)