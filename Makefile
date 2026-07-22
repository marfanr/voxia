ROOT := $(shell pwd)
include scripts/vars.mk

.PHONY: all modules all-hdd kernel iso sbin musl

all: kernel musl lib modules sbin share iso

MUSL_CONFIGURED := ./musl/.configured

musl: $(MUSL_CONFIGURED)
	@mkdir -p $(ISO_DIR)
	$(MAKE) -C musl install DESTDIR=$(ISO_DIR)

$(MUSL_CONFIGURED):
	cd musl && ./configure \
		--prefix=/usr \
		--syslibdir=/lib \
		--enable-static \
		--enable-shared \
		--target=x86_64-voxia
	$(MAKE) -C musl
	touch $@

modules:
	@mkdir -p ./initrd/modules
	$(MAKE) -C ./modules/e1000
	$(MAKE) -C ./modules/ehci
	$(MAKE) -C ./modules/xhci
	$(MAKE) -C ./modules/usb-hid
	$(MAKE) -C ./modules/virtio-gpu
	$(MAKE) -C ./modules/ahci
	$(MAKE) -C ./modules/atapi
	$(MAKE) -C ./modules/sata

lib: musl
	$(MAKE) -C $(ROOT_DIR)/lib/vxair
	$(MAKE) -C $(ROOT_DIR)/lib/vxui

lib-clear:
	$(MAKE) -C $(ROOT_DIR)/lib/vxair clear
	$(MAKE) -C $(ROOT_DIR)/sbin/vxui clear

sbin: lib
	$(MAKE) -C $(ROOT_DIR)/sbin/vshell
	$(MAKE) -C $(ROOT_DIR)/sbin/init
	$(MAKE) -C $(ROOT_DIR)/sbin/toolbox
	$(MAKE) -C $(ROOT_DIR)/sbin/vcomp
	$(MAKE) -C $(ROOT_DIR)/sbin/vxterm

sbin-clean:
	$(MAKE) -C $(ROOT_DIR)/sbin/init clean
	$(MAKE) -C $(ROOT_DIR)/sbin/toolbox clean
	$(MAKE) -C $(ROOT_DIR)/sbin/vcomp clean
	$(MAKE) -C $(ROOT_DIR)/sbin/vxterm clean

all-hdd: $(HDD)

share:
	@rm -rf $(ISO_DIR)/usr/shared
	mkdir -p $(ISO_DIR)/usr/shared
	cp -r $(ROOT_DIR)/shared/* $(ISO_DIR)/usr/shared

# Jalankan dengan QEMU
.PHONY: run run-host run-debug run-gdb run-uefi run-hdd run-hdd-uefi

run:
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO) -boot d  $(QEMU_NETWORK) $(QEMU_USB) -vga none  -device virtio-serial-pci -s \
	-monitor stdio -serial file:qemu.log -d trace:cpu_reset* -D aqemu.log \
	-nographic

debug:
	$(QEMU) $(QEMU_FLAGS) \
		-cdrom $(ISO) \
		-boot d \
		$(QEMU_NETWORK) \
		$(QEMU_USB) \
		-vga none \
		-device virtio-serial-pci \
		-S -s \
		-monitor stdio \
		-serial file:qemu.log \
		-d trace:cpu_reset* \
		-D aqemu.log \
		-nographic &

	gnome-terminal -- gdb kernel.elf \
		-ex "target remote localhost:1234"

run-gpu:                                                                                                
	$(QEMU) $(QEMU_FLAGS) \
	-drive file=/home/arfan/vdisk.img,format=raw,media=disk \
	-cdrom $(ISO) \
	-boot d \
	$(QEMU_USB) $(QEMU_NETWORK) \
	-display gtk,gl=on,full-screen=off \
	-device virtio-gpu-gl-pci,xres=1920,yres=1080,id=gpu,max_outputs=1,edid=on \
	-monitor stdio -serial file:qemu.log -D aqemu.log -s --no-reboot -trace virtio*

run-gpu2:                                                                                                
	$(QEMU) $(QEMU_FLAGS) \
	-drive file=/home/arfan/vdisk.img,format=raw,media=disk \
	-cdrom $(ISO) \
	-boot d \
	$(QEMU_USB) $(QEMU_NETWORK) -vga none \
	-display sdl,gl=on,full-screen=off \
	-device virtio-gpu-gl-pci,xres=1920,yres=1080,id=gpu,max_outputs=1,edid=on \
	-monitor stdio -serial file:qemu.log -D aqemu.log -s --no-reboot -trace virtio*

run-gpu-win: 
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO) -boot d $(QEMU_USB) $(QEMU_NETWORK) \
	-display sdl,gl=on,full-screen=on \
	-device virtio-gpu-gl-pci,xres=1920,yres=1080,id=gpu \
	-monitor stdio -serial file:qemu.log -d trace:*virtio* -D aqemu.log

run-efi: ovmf-x64
	$(QEMU) $(QEMU_FLAGS)  -bios $(BIOS_OVMF) -cdrom $(ISO) -boot d $(QEMU_USB) -vga none  -device virtio-serial-pci -s \
	  -display gtk,gl=on \
	-device virtio-vga-gl,blob=true,hostmem=1G,venus=true \
	-machine accel=kvm \
	-monitor stdio -serial file:qemu.log -d trace:ahci* -D aqemu.log 

run-gpu2-efi: ovmf-x64
	$(QEMU) $(QEMU_FLAGS) \
		-bios $(BIOS_OVMF) \
		-drive file=/home/arfan/vdisk.img,format=raw,media=disk \
		-cdrom $(ISO) \
		-boot d \
		$(QEMU_USB) \
		$(QEMU_NETWORK) \
		-machine accel=kvm \
		-display sdl,gl=on,full-screen=on \
		-device virtio-serial-pci \
		-device virtio-vga-gl,id=gpu,max_outputs=1,edid=on, \
		-monitor stdio \
		-serial file:qemu.log \
		-D aqemu.log \
		-s \
		--no-reboot \
		-trace virtio*

run-venus-headless: ovmf-x64
	$(QEMU) $(QEMU_FLAGS) \
		-bios $(BIOS_OVMF) \
		-drive file=/home/arfan/vdisk.img,format=raw,media=disk \
		-cdrom $(ISO) \
		-boot d \
		$(QEMU_USB) \
		$(QEMU_NETWORK) \
		-machine accel=kvm \
		-display egl-headless \
		-device virtio-serial-pci \
		-device virtio-vga-gl,id=gpu,xres=1920,yres=1080,max_outputs=1,edid=on,blob=true,hostmem=1G,venus=true \
		-monitor stdio \
		-serial file:qemu.log \
		-D aqemu.log \
		-s \
		--no-reboot \
		-trace virtio*

TPM_STATE_DIR = /tmp/tpmstate
TPM_SOCKET    = /tmp/mytpm-sock

run-tpm: stop clean_tpm start_tpm
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO) -boot d $(QEMU_USB) -vga none  -device virtio-serial-pci -s \
	-d int -D qemu.log  \
	  -display gtk,gl=on \
	-device virtio-vga-gl,blob=true,hostmem=1G,venus=true \
	-chardev socket,id=chrtpm,path=$(TPM_SOCKET) \
	-tpmdev emulator,id=tpm0,chardev=chrtpm \
	-device tpm-tis,tpmdev=tpm0 \
	-monitor stdio -serial file:qemu.log

start_tpm:
	@echo "[SWTPM] Starting TPM emulator..."
	@mkdir -p $(TPM_STATE_DIR)
	@chmod 777 $(TPM_STATE_DIR)
	@swtpm socket --tpm2 \
		--ctrl type=unixio,path=$(TPM_SOCKET) \
		--tpmstate dir=$(TPM_STATE_DIR) \
		--log level=20 & \
	sleep 1
	@echo "[SWTPM] Ready."

stop:
	@echo "[SWTPM] Stopping existing swtpm..."
	@-pkill swtpm || true

clean_tpm:
	@echo "[CLEAN] Removing old TPM state..."
	@rm -rf $(TPM_STATE_DIR) $(TPM_SOCKET)

run-host: 
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO) -boot d $(QEMU_USB) -vga std -device virtio-serial-pci -cpu host -d int,cpu_reset,guest_errors

run-debug: 
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO) -boot d -S -s $(QEMU_USB) -vga std -device virtio-serial-pci

run-gdb: iso
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO) -boot d -s $(QEMU_USB) -device e1000-82544gc,netdev=nic1,id=e1000 -netdev bridge,id=nic1,br=br0 -d int,cpu_reset -no-reboot -S

run-uefi: ovmf-x64 iso
	$(QEMU) $(QEMU_FLAGS) -bios $(BIOS_OVMF) -cdrom $(ISO) -boot d

run-hdd: iso
	$(QEMU) $(QEMU_FLAGS) -hda $(ISO) -boot order=a -accel kvm

run-hdd-uefi: ovmf-x64 $(HDD)
	$(QEMU) $(QEMU_FLAGS) -bios $(BIOS_OVMF) -hda $(HDD)

# Build dependencies
rust:
	$(MAKE) -C rust

ovmf-x64:
	mkdir -p ovmf-x64
	cd ovmf-x64 && curl --fail -o OVMF-X64.zip https://efi.akeo.ie/OVMF/OVMF-X64.zip && 7z x OVMF-X64.zip

limine:
	@if [ ! -d limine ]; then \
		git clone https://github.com/limine-bootloader/limine.git --branch=v11.x-binary --depth=1; \
	fi
	$(MAKE) -C limine

kernel:
	mkdir -p build/kernel
	$(MAKE) -C kernel	

iso: kernel musl modules sbin limine $(MUSL_CONFIGURED)
	mkdir -p $(ISO_DIR)
	mkdir -p $(ISO_DIR)/boot/limine
	cd $(INITRD_DIR) && tar -F ustar -cvf $(realpath $(ISO_DIR))/initrd.tar .
	cp $(KERNEL_ELF) $(ISO_DIR)/
	cp limine.conf $(ISO_DIR)/
	cp limine.conf $(ISO_DIR)/limine.cfg
	cp limine.conf $(ISO_DIR)/boot/limine/
	cp limine.conf $(ISO_DIR)/boot/limine/limine.cfg
	cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin $(ISO_DIR)/
	cp limine/limine-bios.sys $(ISO_DIR)/boot/limine/
	xorriso -as mkisofs -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $(ISO)
	./limine/limine bios-install $(ISO)

# Build HDD image
$(HDD):
	rm -f elysia.hdd
	dd if=/dev/zero bs=1M count=0 seek=64 of=elysia.hdd
	parted -s elysia.hdd mklabel gpt
	parted -s elysia.hdd mkpart BIOSBOOT 1MiB 2MiB
	parted -s elysia.hdd set 1 bios_grub on
	parted -s elysia.hdd mkpart ESP fat32 2MiB 100%
	parted -s elysia.hdd set 2 esp on
	./limine/limine bios-install elysia.hdd
	sudo losetup -Pf --show elysia.hdd >loopback_dev
	sudo mkfs.fat -F 32 `cat loopback_dev`p2
	mkdir -p img_mount
	sudo mount `cat loopback_dev`p2 img_mount
	sudo mkdir -p img_mount/EFI/BOOT
	sudo mkdir -p img_mount/boot/limine
	sudo cp -v $(KERNEL_ELF) img_mount/
	sudo cp -v limine.conf img_mount/
	sudo cp -v limine.conf img_mount/limine.cfg
	sudo cp -v limine.conf img_mount/boot/limine/
	sudo cp -v limine.conf img_mount/boot/limine/limine.cfg
	sudo cp -v limine/limine-bios.sys img_mount/boot/limine/
	sudo cp -v limine/BOOTX64.EFI img_mount/EFI/BOOT/
	sync
	sudo umount img_mount
	sudo losetup -d `cat loopback_dev`
	sudo rm -rf loopback_dev img_mount


# Flashdisk setup
FD_DEV ?= /dev/sdb
FD_PART ?= $(FD_DEV)2

.PHONY: flashdisk run-fd
flashdisk: all
	@echo "\n🚨 WARNING: This will erase $(FD_DEV). Press Ctrl+C to cancel!" && sleep 5
	sudo parted -s $(FD_DEV) mklabel gpt
	sudo parted -s $(FD_DEV) mkpart BIOSBOOT 1MiB 2MiB
	sudo parted -s $(FD_DEV) set 1 bios_grub on
	sudo parted -s $(FD_DEV) mkpart ESP fat32 2MiB 100%
	sudo parted -s $(FD_DEV) set 2 esp on
	sudo ./limine/limine bios-install $(FD_DEV)
	sudo mkfs.fat -F 32 $(FD_PART)
	mkdir -p usb_mount
	mkdir -p $(INITRD_DIR)/sbin
	cp -r $(ISO_DIR)/sbin/* $(INITRD_DIR)/sbin/ || true
	sudo mount $(FD_PART) usb_mount
	cd $(INITRD_DIR) && sudo tar -F ustar -cvf $(ROOT)/usb_mount/initrd.tar .
	sudo mkdir -p usb_mount/EFI/BOOT
	sudo mkdir -p usb_mount/boot/limine
	sudo cp -v $(KERNEL_ELF) usb_mount/
	sudo cp -v limine.conf usb_mount/
	sudo cp -v limine.conf usb_mount/limine.cfg
	sudo cp -v limine.conf usb_mount/boot/limine/
	sudo cp -v limine.conf usb_mount/boot/limine/limine.cfg
	sudo cp -v limine/limine-bios.sys usb_mount/boot/limine/
	sudo cp -v limine/BOOTX64.EFI usb_mount/EFI/BOOT/
	sync
	sudo umount usb_mount
	sudo rm -rf usb_mount
	@echo "✅ Flashdisk bootable selesai dibuat di $(FD_DEV)!"

run-fd:
	sudo $(QEMU) $(QEMU_FLAGS) -hda $(FD_DEV) $(QEMU_USB) $(QEMU_NETWORK) \
	-display gtk,gl=on,full-screen=off \
	-device virtio-gpu-gl-pci,xres=1920,yres=1080,id=gpu \
	-device virtio-serial-pci -m 2G -serial file:qemu.log

# Cleanup
.PHONY: clean distclean sbin-clean doc-clean
clean:
	rm -rf $(ISO_DIR) $(ISO) $(BUILD_DIR) $(SYSROOT)
	$(MAKE) -C kernel clean	
	$(MAKE) -C modules/xhci clean
	$(MAKE) -C modules/ehci clean
	$(MAKE) -C modules/usb-hid clean
	$(MAKE) -C modules/e1000 clean
	$(MAKE) -C modules/virtio-gpu clean
	$(MAKE) -C modules/ahci clean
	$(MAKE) -C modules/atapi clean
	$(MAKE) -C modules/sata clean

musl-clean:
	rm -f $(MUSL_CONFIGURED)
	$(MAKE) -C musl clean

distclean: clean doc-clean
	rm -rf limine ovmf-x64
	$(MAKE) -C kernel distclean
	-$(MAKE) -C musl distclean

defconfig:
	yes "" | kconfig-conf --oldconfig Kconfig > .config
	make config

menuconfig:
	kconfig-mconf Kconfig
	make config

config: .config
	@mkdir -p generated
	@echo "// Auto-generated from .config" > include/autoconf.h
	@grep -E '^CONFIG_' .config | sed \
		-e 's/=y/ 1/' \
		-e 's/=n/ 0/' \
		-e 's/=/ /' \
		-e 's/^CONFIG_/ #define VOXIA_/' >> include/autoconf.h

# Documentation
DOXYGEN_VER := 1.13.2
DOXYGEN_BIN := tools/docs/doxygen-$(DOXYGEN_VER)/bin/doxygen
THEME_CSS := tools/docs/doxygen-awesome.css

$(DOXYGEN_BIN):
	@echo "[DOC] Downloading Doxygen $(DOXYGEN_VER)..."
	@mkdir -p tools/docs
	@curl -sSL https://www.doxygen.nl/files/doxygen-$(DOXYGEN_VER).linux.bin.tar.gz | tar -xz -C tools/docs/
	@touch $@

$(THEME_CSS):
	@echo "[DOC] Downloading theme..."
	@mkdir -p tools/docs
	@curl -sSL --fail https://raw.githubusercontent.com/jothepro/doxygen-awesome-css/main/doxygen-awesome.css -o $@

.PHONY: doc
doc: $(DOXYGEN_BIN) $(THEME_CSS) docs/custom.css
	@echo "[DOC] Generating documentation..."
	@ROOT=$(ROOT) $(DOXYGEN_BIN) docs/Doxyfile
	@echo "[DOC] Done. Open docs/html/index.html in your browser."

doc-clean:
	rm -rf docs/html docs/latex tools/docs/doxygen-* $(THEME_CSS)