C_FILES := $(shell find . -type f -name '*.c')
S_FILES := $(shell find . -type f -name '*.asm' ! -path './hal/cpu/core_ap.asm')
DFILES  := $(C_FILES:.c=.d)
FONT_OBJ := ../resources/fonts/default.o 
OBJ := $(C_FILES:.c=.o) $(S_FILES:.asm=_asm.o)
ROOT ?= $(shell pwd)/..
include $(ROOT)/scripts/vars.mk

# Verbosity control
V ?= 0
ifeq ($(V),1)
	Q :=
	E := @true
else
	Q := @
	E := @echo
endif

KERNEL_SSE_FLAGS := -mno-sse -mno-sse2 -mno-mmx -mno-avx -mno-avx2 \
                 -mno-red-zone -mno-80387 -mno-3dnow 

WARNING := -Wall -Wextra -Werror -Wshadow -Wpointer-arith -Wcast-align \
	-Wstrict-prototypes -Wmissing-prototypes -Wundef -Wconversion \
	-Wdouble-promotion -Wvla -Wstrict-aliasing=2 -Warray-bounds \
	-Wformat=2 -Wuninitialized -g3

CFLAGS := $(WARNING) -O3 -pipe -ggdb -g -fno-strict-aliasing \
                                        -std=gnu2x -ffreestanding \
                                        -fno-stack-protector -fno-stack-clash-protection -fPIC \
                                        -fvisibility=hidden \
                                        $(KERNEL_SSE_FLAGS) \
                                        -I . -I ../include -nostdlib -nostartfiles -I $(ROOT)/generated \
                                        -nodefaultlibs -nostdinc -fno-builtin \
                        -mcmodel=medium -fno-omit-frame-pointer \
						-MMD -MP

CFLAGS := $(CFLAGS)

LDFLAGS := -nostdlib -static \
					 -z max-page-size=0x1000 \
					 -T linker.ld --no-dynamic-linker \
					 -pie -shared -Bsymbolic

.PHONY: all
all: $(OBJ)
	$(E) "  LD      $(notdir $(KERNEL_ELF))"
	$(AS) -f bin ./hal/cpu/core_ap.asm -o ./hal/cpu/core_ap.bin
	$(OBJCOPY) --input-target=binary \
        --output-target=elf64-x86-64 \
        --binary-architecture=i386 \
        hal/cpu/core_ap.bin hal/cpu/core_ap.o
	rm -f hal/cpu/core_ap.bin
	$(LD) $(LDFLAGS) $(OBJ) hal/cpu/core_ap.o -o $(KERNEL_ELF)
# 	$(E) "  EXPORT  $(notdir $(KERNEL_ELF)) (enforcing .export API)"
# 	$(Q)SEC_INDEX=$$(readelf -W -S $(KERNEL_ELF) | awk '$$2 == ".export" {print $$1}' | tr -d '[]'); \
# 	if [ -n "$$SEC_INDEX" ]; then \
# 		echo "{" > exports.map; \
# 		echo "  global:" >> exports.map; \
# 		readelf -W -s $(KERNEL_ELF) | \
# 		awk -v idx="$$SEC_INDEX" '$$7 == idx && $$8 != "" {print $$8}' | \
# 		grep -vE "^_|^@" | \
# 		awk '{print "    " $$1 ";"}' >> exports.map; \
# 		echo "  local: *;" >> exports.map; \
# 		echo "};" >> exports.map; \
# 		$(LD) $(LDFLAGS) \
# 			--version-script=exports.map \
# 			$(OBJ) hal/cpu/core_ap.o \
# 			-o $(KERNEL_ELF); \
# 		rm exports.map; \
# 	fi



-include $(DFILES)

# ssfn.h is a third-party header incompatible with strict warnings
SSFN_SUPPRESS := -Wno-sign-conversion -Wno-cast-align \
                 -Wno-implicit-int-conversion -Wno-unknown-pragmas \
                 -Wno-missing-prototypes -Wno-error

hal/graphic/graphic.o: hal/graphic/graphic.c
	$(E) "  CC      $(notdir $<)"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(SSFN_SUPPRESS) -c $< -o $@

%.o: %.c
	$(E) "  CC      $(notdir $<)"
	$(Q)mkdir -p $(dir $@)
	@$(Q)$(CC) $(CFLAGS) -c $< -o $@

%_asm.o: %.asm
	$(E) "  AS      $(notdir $<)"
	@$(Q)$(AS) -f elf64 $< -o $@

.PHONY: clean
clean:
	$(E) "  CLEAN   $(notdir $(KERNEL_ELF)) $(notdir $(OBJ))"
	$(Q)rm -f $(KERNEL_ELF) $(OBJ) $(DFILES)