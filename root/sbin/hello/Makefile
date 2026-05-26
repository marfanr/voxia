# Sesuaikan path ini dengan lokasi absolut sysroot kamu
SYSROOT = ../../root/sysroot

# Compiler dan Target Architecture
CC = clang
TARGET = x86_64-linux-musl

# Flags krusial: 
# --sysroot memaksa Clang mencari include/ dan lib/ di folder OS-mu, bukan host.
# -static memaksa proses linking menggunakan libc.a (bukan libc.so).
# CFLAGS = --target=$(TARGET) --sysroot=$(SYSROOT) -static -Wall -Wextra -O2
CFLAGS = --config ./voxia.cfg

# Nama file output dan source
TARGET_BIN = hello.elf
SRC = src/main.c

all: $(TARGET_BIN)

$(TARGET_BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET_BIN)
	cp $@ ../../root/sbin/

clean:
	rm -f $(TARGET_BIN)