CPP_FILES=$(shell find . -type f -name '*.c')
OBJ=$(CPP_FILES:.c=.o)

.PHONY: all
all: $(OBJ)
	mkdir -p ../../build/modules
	ld -o ../../build/modules/flui.elf -lc -e main -m elf_x86_64 $(OBJ)

%.o:%.c
	mkdir -p $(dir $@)
	gcc -c -Wall -Wextra -O2 -pipe -ggdb -g -std=gnu11 -c $< -o $@