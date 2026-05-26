# Common variables for Voxia OS build system

# Toolchain
CC      := clang
LD      := ld
AS      := nasm
OBJCOPY := objcopy
AR      := ar
RANLIB  := ranlib

# QEMU Configuration
QEMU          := qemu-system-x86_64
QEMU_FLAGS    := -m 3G -cpu host -M q35 -smp 2 -enable-kvm -rtc base=localtime
QEMU_USB      := -device usb-ehci,id=ehci -device usb-kbd,bus=ehci.0,port=1,id=kbd
QEMU_NETWORK  := -netdev tap,id=net0,ifname=tap0,script=no,downscript=no -device e1000e,netdev=net0

# Build Artifacts
ISO           := naya.iso
HDD           := barebones.hdd
BIOS_OVMF     := $(ROOT)/ovmf-x64/OVMF.fd

# Directories
BUILD_DIR     := $(ROOT)/build
SYSROOT       := $(ROOT)/sysroot
ISO_DIR       := $(ROOT)/iso_root
ROOT_DIR      := $(ROOT)/root
INITRD_DIR    := $(ROOT)/initrd
KERNEL_ELF    := $(BUILD_DIR)/kernel.elf

# Exporting variables to sub-makes
export CC LD AS OBJCOPY AR RANLIB
export QEMU QEMU_FLAGS QEMU_USB QEMU_NETWORK
export ISO HDD BIOS_OVMF
export BUILD_DIR SYSROOT ISO_DIR ROOT_DIR INITRD_DIR KERNEL_ELF
