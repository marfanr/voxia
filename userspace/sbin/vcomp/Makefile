ROOT ?= $(shell pwd)/../../..

include $(ROOT)/scripts/vars.mk

CC = clang
TARGET = x86_64-linux-musl

CFLAGS = -g -O3 --config ./voxia.cfg -I$(ROOT_DIR)/lib/vxair/include

TARGET_BIN = vcomp
SRC = $(shell find src -type f -name '*.c')

all: $(TARGET_BIN)
	@mkdir -p $(ISO_DIR)/sbin/
	cp $(TARGET_BIN) $(ISO_DIR)/sbin/

$(TARGET_BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET_BIN)

clean:
	rm -f $(TARGET_BIN)