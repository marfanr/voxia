ROOT := $(shell pwd)
include scripts/vars.mk

.PHONY: all modules all-hdd kernel iso sbin musl

all: kernel musl modules sbin iso

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
	$(MAKE) -C ./modules/usb-hid
	$(MAKE) -C ./modules/virtio-gpu
	$(MAKE) -C ./modules/ahci
	$(MAKE) -C ./modules/atapi
# 	$(MAKE) -C ./modules/runtimeinit all

sbin:
	$(MAKE) -C $(ROOT_DIR)/sbin/hello

sbin-clean:
	@rm -rf $(ISO_DIR)/root/sbin
	$(MAKE) -C $(ROOT_DIR)/sbin/hello clean

all-hdd: $(HDD)

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

run-gpu2:
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO) -boot d $(QEMU_USB) $(QEMU_NETWORK) \
	-display gtk,gl=on,full-screen=on \
	-device virtio-gpu-gl-pci,xres=1920,yres=1080,id=gpu \
	-monitor stdio -serial file:qemu.log -d trace:*virtio* -D aqemu.log -s

run-gpu-win: 
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO) -boot d $(QEMU_USB) $(QEMU_NETWORK) \
	-display sdl,gl=on,full-screen=on \
	-device virtio-gpu-gl-pci,xres=1920,yres=1080,id=gpu \
	-monitor stdio -serial file:qemu.log -d trace:*virtio* -D aqemu.log

run-efi: ovmf-x64
	$(QEMU) $(QEMU_FLAGS)  -bios $(BIOS_OVMF) -cdrom $(ISO) -boot d $(QEMU_USB) -vga none  -device virtio-serial-pci -s \
	  -display sdl,gl=on \
	-device virtio-vga-gl \
	-machine accel=kvm \
	-monitor stdio -serial file:qemu.log -d trace:ahci* -D aqemu.log 


TPM_STATE_DIR = /tmp/tpmstate
TPM_SOCKET    = /tmp/mytpm-sock

run-tpm: stop clean_tpm start_tpm
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO) -boot d $(QEMU_USB) -vga none  -device virtio-serial-pci -s \
	-d int -D qemu.log  \
	  -display sdl,gl=on \
	-device virtio-vga-gl \
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
	cd ovmf-x64 && curl -o OVMF-X64.zip https://efi.akeo.ie/OVMF/OVMF-X64.zip && 7z x OVMF-X64.zip

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v2.0-branch-binary --depth=1
	$(MAKE) -C limine

kernel:
	mkdir -p build/kernel
	$(MAKE) -C kernel	

iso: kernel musl modules sbin limine $(MUSL_CONFIGURED)
	mkdir -p $(ISO_DIR)
	cd $(INITRD_DIR) && tar -F ustar -cvf $(realpath $(ISO_DIR))/initrd.tar *
	cp $(KERNEL_ELF) limine.cfg limine/limine.sys limine/limine-cd.bin limine/limine-eltorito-efi.bin $(ISO_DIR)/
	xorriso -as mkisofs -b limine-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-eltorito-efi.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $(ISO)
	limine/limine-install $(ISO)

# Build HDD image
$(HDD):
	rm -f elysia.hdd
	dd if=/dev/zero bs=1M count=0 seek=64 of=elysia.hdd
	parted -s elysia.hdd mklabel gpt
	parted -s elysia.hdd mkpart ESP fat32 2048s 100%
	parted -s elysia.hdd set 1 esp on
	limine/limine-deploy elysia.hdd
	sudo losetup -Pf --show elysia.hdd >loopback_dev
	sudo mkfs.fat -F 32 `cat loopback_dev`p1
	mkdir -p img_mount
	sudo mount `cat loopback_dev`p1 img_mount
	sudo mkdir -p img_mount/EFI/BOOT
	sudo cp -v $(KERNEL_ELF) limine.cfg limine/limine.sys img_mount/
	sudo cp -v limine/BOOTX64.EFI img_mount/EFI/BOOT/
	sync
	sudo umount img_mount
	sudo losetup -d `cat loopback_dev`
	sudo rm -rf loopback_dev img_mount

# Flashdisk setup
.PHONY: flashdisk run-flashdisk
flashdisk:
	echo "\n🚨 WARNING: This will erase /dev/sdb. Press Ctrl+C to cancel!" && sleep 5
	sudo parted -s /dev/sdb mklabel gpt
	sudo parted -s /dev/sdb mkpart ESP fat32 2048s 100%
	sudo parted -s /dev/sdb set 1 esp on
	sudo limine/limine-install /dev/sdb
	sudo mkfs.fat -F 32 /dev/sdb1
	mkdir -p usb_mount
	cp -r build/modules ./initrd
	sudo mount /dev/sdb1 usb_mount
	cd initrd; tar -F ustar -cvf ../usb_mount/initrd.tar *; cd ..
	sudo mkdir -p usb_mount/EFI/BOOT
	sudo cp -v $(KERNEL_ELF) limine.cfg limine/limine.sys usb_mount/
	sudo cp -v limine/BOOTX64.EFI usb_mount/EFI/BOOT/
	sync
	sudo umount usb_mount
	sudo rm -rf usb_mount
	echo "✅ Flashdisk bootable selesai dibuat di /dev/sdb!"

run-flashdisk:
	$(QEMU) $(QEMU_FLAGS) -hda /dev/sdb $(QEMU_USB) -vga std -device virtio-serial-pci

# Cleanup
.PHONY: clean distclean sbin-clean
clean:
	rm -rf $(ISO_DIR) $(ISO) $(BUILD_DIR) $(SYSROOT)
	$(MAKE) -C kernel clean	
	$(MAKE) -C modules/ehci clean
	$(MAKE) -C modules/usb-hid clean
	$(MAKE) -C modules/e1000 clean
	$(MAKE) -C modules/virtio-gpu clean
	$(MAKE) -C modules/ahci clean
	$(MAKE) -C modules/atapi clean

musl-clean:
	-$(MAKE) -C musl clean

distclean: clean
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

$(DOXYGEN_BIN):
	@echo "[DOC] Downloading Doxygen $(DOXYGEN_VER)..."
	@mkdir -p tools/docs
	@curl -sSL https://www.doxygen.nl/files/doxygen-$(DOXYGEN_VER).linux.bin.tar.gz | tar -xz -C tools/docs/
	@touch $@

.PHONY: doc
doc: $(DOXYGEN_BIN)
	@echo "[DOC] Generating documentation..."
	@ROOT=$(ROOT) $(DOXYGEN_BIN) docs/Doxyfile
	@echo "[DOC] Done. Open docs/html/index.html in your browser."

doc-clean:
	rm -rf docs/html docs/latex tools/docs/doxygen-*