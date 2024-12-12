CPP_FILES=$(shell find . -type f -name '*.c')
S_FILES=$(shell find . -type f -name '*.asm')
DFILES=$(CPP_FILES:.c=.d)
FONT_OBJ=../resources/fonts/default.o 
OBJ=$(CPP_FILES:.c=.o) $(S_FILES:.asm=_asm.o)
KERNEL=../build/sources.elf

CFLAGS= -Wall -Wextra -O2 -pipe -ggdb -g \
		-std=gnu11				\
	-ffreestanding			\
	-fno-stack-protector	\
	-fno-pic -fpie			\
	-mno-80387				\
	-mno-mmx				\
	-mno-3dnow				\
	-mno-sse				\
	-mno-sse2				\
	-mno-red-zone		\
	-I . \
	-nostdlib -nostartfiles \
	-nodefaultlibs -nostdinc -fno-builtin 

# Internal linker flags that should not be changed by the user.
LDFLAGS = -nostdlib               \
	-static                 \
	-m elf_x86_64           \
	-z max-page-size=0x1000 \
	-T linker.ld	\
	--no-dynamic-linker \
	-ztext

.PHONY: all
all: $(OBJ)	
	ld $(LDFLAGS) $(OBJ) -o $(KERNEL)

-include $(DFILES)
	
%.o:%.c	
	mkdir -p $(dir $@)
	gcc $(CFLAGS) -c $< -o $@	

%_asm.o:%.asm
	nasm -f elf64 $< -o $@

.PHONY: clean
clean:
	rm -f $(KERNEL) $(OBJ) $(DFILES)	
