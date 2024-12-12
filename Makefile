.PHONY: all modules
all: iso

.PHONY: modules
modules:
	make -C ./modules/flui all

.PHONY: all-hdd
all-hdd: barebones.hdd

.PHONY: run
run: iso
	qemu-system-x86_64 -M q35 -m 4G -cdrom naya.iso -boot d  -s -serial stdio -enable-kvm  -device usb-ehci,id=ehci \
	 -device usb-kbd,bus=ehci.0,port=1,id=kbd -device ati-vga  \
	-rtc base=localtime	-device e1000-82544gc,netdev=nic1,id=e1000 -netdev bridge,id=nic1,br=br0
	# -usb -device usb-host,vendorid=0x1a2c,productid=0x0b2a

.PHONY: run-gdb
run-gdb: iso
	qemu-system-x86_64 -M q35 -m 2G -rtc base=localtime -cdrom naya.iso -boot d -enable-kvm -s -serial stdio -d trace:usb_ehci_opreg* -device usb-ehci,id=ehci -device usb-kbd,bus=ehci.0,port=2,id=kbd \
	-device e1000-82544gc,netdev=nic1,id=e1000 -netdev bridge,id=nic1,br=br0

.PHONY: run-uefi
run-uefi: ovmf-x64 iso
	qemu-system-x86_64 -M q35 -m 2G -bios ovmf-x64/OVMF.fd -cdrom iso -boot d

.PHONY: run-hdd
run-hdd: iso
	qemu-system-x86_64 -M q35 -m 4G -accel kvm -hda naya.iso -smp 2 \
	-s -serial stdio -device usb-ehci,id=ehci -device usb-mouse,bus=ehci.0,port=1,id=mouse -device ati-vga -device secondary-vga \
	-boot order=a

.PHONY: run-hdd-uefi
run-hdd-uefi: ovmf-x64 barebones.hdd
	qemu-system-x86_64 -M q35 -m 2G -bios ovmf-x64/OVMF.fd -hda barebones.hdd

ovmf-x64:
	mkdir -p ovmf-x64
	cd ovmf-x64 && curl -o OVMF-X64.zip https://efi.akeo.ie/OVMF/OVMF-X64.zip && 7z x OVMF-X64.zip

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v2.0-branch-binary --depth=1
	make -C limine

.PHONY: sources
sources:
	mkdir -p build/sources
	$(MAKE) -C sources	

iso: limine sources modules
	rm -rf iso_root
	mkdir -p iso_root
	cp -r build/modules ./initrd
	cd initrd;tar -F ustar -cvf ../iso_root/initrd.tar *;cd ..
	cp build/sources.elf \
		limine.cfg limine/limine.sys limine/limine-cd.bin limine/limine-eltorito-efi.bin iso_root/
	xorriso -as mkisofs -b limine-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-eltorito-efi.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o naya.iso
	limine/limine-install naya.iso	

barebones.hdd: limine sources
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
	sudo cp -v build/sources.elf limine.cfg limine/limine.sys img_mount/
	sudo cp -v limine/BOOTX64.EFI img_mount/EFI/BOOT/
	sync
	sudo umount img_mount
	sudo losetup -d `cat loopback_dev`
	sudo rm -rf loopback_dev img_mount

.PHONY: clean
clean:
	rm -rf iso_root iso naya.hdd build naya.iso
	$(MAKE) -C sources clean	

.PHONY: distclean
distclean: clean
	rm -rf limine ovmf-x64
	$(MAKE) -C souce distclean
