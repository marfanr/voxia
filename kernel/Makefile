C_FILES := $(shell find . -type f -name '*.c')
S_FILES := $(shell find . -type f -name '*.asm')
DFILES  := $(C_FILES:.c=.d)
FONT_OBJ := ../resources/fonts/default.o 
OBJ := $(C_FILES:.c=.o) $(S_FILES:.asm=_asm.o)
KERNEL := ../build/kernel.elf

# Verbosity control
V ?= 0
ifeq ($(V),1)
	Q :=
	E := @true
else
	Q := @
	E := @echo
endif

CFLAGS := -Wall -Wextra -O0 -pipe -ggdb -g \
					-std=gnu2x -ffreestanding \
					-fno-stack-protector -fno-pie \
					-mno-80387 -mno-3dnow \
					-mfpmath=sse -msse -msse2  -mmmx \
					-mno-red-zone \
					-I . -nostdlib -nostartfiles \
					-nodefaultlibs -nostdinc -fno-builtin \
			-mcmodel=kernel -Wdeprecated-declarations -fvisibility=hidden 

LDFLAGS := -nostdlib -static \
					 -z max-page-size=0x1000 \
					 -T linker.ld --no-dynamic-linker

RUST_KERNEL := ../rust/target/x86_64-unknown-none/release/libkernel.a

.PHONY: all
all: $(OBJ)
	$(E) "  LD     $(notdir $(KERNEL))"
	$(Q)ld.lld $(LDFLAGS) $(OBJ) -o $(KERNEL)
	objdump -t $(KERNEL) | grep '\.export' | awk '{print $$6}' > keep.txt
	objcopy --strip-unneeded --keep-symbols=keep.txt  $(KERNEL) $(KERNEL)
# 	nm -S --defined-only $(KERNEL) | grep '\.export' >> keep.txt
# 	nm $(KERNEL) | grep "__export_" | awk '{print $$3}'t > keep.txt

-include $(DFILES)

%.o: %.c
	$(E) "  CC      $(notdir $<)"
	$(Q)mkdir -p $(dir $@)
	@$(Q)clang $(CFLAGS) -c $< -o $@

%_asm.o: %.asm
	$(E) "  AS      $(notdir $<)"
	@$(Q)nasm -f elf64 $< -o $@

.PHONY: clean
clean:
	$(E) "  CLEAN   $(notdir $(KERNEL)) $(notdir $(OBJ))"
	$(Q)rm -f $(KERNEL) $(OBJ) $(DFILES)