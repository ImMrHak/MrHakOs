# MrHakOS Makefile - 32-bit + 64-bit
# Works with either an OSDev i686-elf toolchain or Kali/Debian's
# i686-linux-gnu cross toolchain.

BUILD_DIR := bin
SRC_DIR   := src

# Prefer the freestanding OSDev toolchain when installed, otherwise fall back
# to Kali/Debian package names installed by gcc-i686-linux-gnu/g++-i686-linux-gnu.
I686_PREFIX := $(if $(shell command -v i686-elf-g++ 2>/dev/null),i686-elf,$(if $(shell command -v i686-linux-gnu-g++ 2>/dev/null),i686-linux-gnu,i686-elf))

CC      = $(I686_PREFIX)-gcc
CXX     = $(I686_PREFIX)-g++
LD      = $(I686_PREFIX)-ld
OBJCOPY = $(I686_PREFIX)-objcopy
AS      = nasm

X64_CXX     = clang++
X64_LD      = ld.lld
X64_OBJCOPY = llvm-objcopy

QEMU32 = qemu-system-i386
QEMU64 = qemu-system-x86_64
QEMU_SENDKEYS := scripts/qemu_sendkeys.py
SMOKE_COMMANDS := help\nmkdir docs\ntouch hello.hak\necho hello > hello.hak\ncat hello.hak\nls\n

BOOT_SRC     := $(SRC_DIR)/boot/bootloader.asm
BOOT64_SRC   := $(SRC_DIR)/boot/bootloader64.asm
KERNEL_ASM   := $(SRC_DIR)/kernel/entry.asm
KERNEL64_ASM := $(SRC_DIR)/kernel/entry64.asm
KERNEL_CPP   := $(SRC_DIR)/kernel/kernel.cpp

LIBC_CPP_VGA  := $(SRC_DIR)/libc/vga.cpp
LIBC_CPP_STR  := $(SRC_DIR)/libc/string.cpp
LIBC_CPP_TERM := $(SRC_DIR)/libc/terminal.cpp
LIBC_CPP_INT  := $(SRC_DIR)/libc/interrupts.cpp
LIBC_CPP_IDT  := $(SRC_DIR)/libc/idt.cpp
LIBC_CPP_FS   := $(SRC_DIR)/libc/filesystem.cpp
LIBC_CPP_SERIAL := $(SRC_DIR)/libc/serial.cpp
LIBC_CPP_PCI    := $(SRC_DIR)/libc/pci.cpp
LIBC_CPP_NET    := $(SRC_DIR)/libc/network.cpp
LIBC_CPP_MEM    := $(SRC_DIR)/libc/memory.cpp
LIBC_CPP_CRYPTO := $(SRC_DIR)/libc/crypto.cpp
LIBC_ASM_ISR  := $(SRC_DIR)/libc/isr.asm
LIBC_ASM_ISR64:= $(SRC_DIR)/libc/isr64.asm

BOOT_BIN      := $(BUILD_DIR)/boot.bin
KERNEL_ELF    := $(BUILD_DIR)/kernel.elf
KERNEL_BIN    := $(BUILD_DIR)/kernel.bin
IMAGE_FILE    := $(BUILD_DIR)/mrhakos.img
BOOTABLE_BIN  := $(BUILD_DIR)/mrhakos-bootable.bin
BOOT64_BIN    := $(BUILD_DIR)/boot64.bin
KERNEL64_ELF  := $(BUILD_DIR)/kernel64.elf
KERNEL64_BIN  := $(BUILD_DIR)/kernel64.bin
IMAGE_FILE_64 := $(BUILD_DIR)/mrhakos64.img
GRUB_ISO      := $(BUILD_DIR)/mrhakos-grub.iso
GRUB_ISO_SHA  := $(GRUB_ISO).sha256
GRUB_ISODIR   := $(BUILD_DIR)/grub-isodir
GRUB_MENU_CFG := $(BUILD_DIR)/41_mrhakos
OVMF_PATHS    := /usr/share/OVMF/OVMF_CODE.fd /usr/share/ovmf/OVMF.fd /usr/share/qemu/OVMF.fd
OVMF_FD      := $(firstword $(wildcard $(OVMF_PATHS)))

FLOPPY_SIZE_BYTES := 1474560
SECTOR_SIZE       := 512
KERNEL_SECTORS_32 := 160
KERNEL_SECTORS_64 := 160
KERNEL_MAX_32     := $(shell expr $(KERNEL_SECTORS_32) \* $(SECTOR_SIZE))
KERNEL_MAX_64     := $(shell expr $(KERNEL_SECTORS_64) \* $(SECTOR_SIZE))

OBJS := \
	$(BUILD_DIR)/entry.o \
	$(BUILD_DIR)/kernel.o \
	$(BUILD_DIR)/string.o \
	$(BUILD_DIR)/vga.o \
	$(BUILD_DIR)/terminal.o \
	$(BUILD_DIR)/interrupts.o \
	$(BUILD_DIR)/idt.o \
	$(BUILD_DIR)/filesystem.o \
	$(BUILD_DIR)/serial.o \
	$(BUILD_DIR)/memory.o \
	$(BUILD_DIR)/crypto.o \
	$(BUILD_DIR)/pci.o \
	$(BUILD_DIR)/network.o \
	$(BUILD_DIR)/isr.o

X64_OBJS := \
	$(BUILD_DIR)/entry64.o \
	$(BUILD_DIR)/kernel64.o \
	$(BUILD_DIR)/string64.o \
	$(BUILD_DIR)/vga64.o \
	$(BUILD_DIR)/terminal64.o \
	$(BUILD_DIR)/interrupts64_cpp.o \
	$(BUILD_DIR)/filesystem64.o \
	$(BUILD_DIR)/serial64.o \
	$(BUILD_DIR)/memory64.o \
	$(BUILD_DIR)/crypto64.o \
	$(BUILD_DIR)/pci64.o \
	$(BUILD_DIR)/network64.o \
	$(BUILD_DIR)/isr64.o

INCLUDES := -I$(SRC_DIR)/libc/include
COMMON_CXXFLAGS := -ffreestanding -Os -nostdlib -fno-exceptions -fno-rtti -fcheck-new -Wall -Wextra $(INCLUDES)
CFLAGS   := -ffreestanding -Os -nostdlib -Wall -Wextra $(INCLUDES)
CXXFLAGS := $(COMMON_CXXFLAGS)
X64_CXXFLAGS := -target x86_64-elf -D__x86_64__ $(COMMON_CXXFLAGS) -mno-red-zone -mgeneral-regs-only -Wno-new-returns-null
LDFLAGS  := -T $(SRC_DIR)/kernel/linker.ld -nostdlib
X64_LDFLAGS := -T $(SRC_DIR)/kernel/linker64.ld -nostdlib

.PHONY: all all32 all64 run run32 run32-net run64 run64-net run-grub run-uefi check-tools check-grub-tools doctor install-deps-help check-sizes32 check-sizes64 smoke smoke32 smoke64 smoke32-net smoke64-net iso grubiso iso-checksum boot-report grub-menu-config grub-assets clean

all: all32

all32: $(IMAGE_FILE) $(BOOTABLE_BIN)

all64: $(IMAGE_FILE_64)

check-tools:
	@echo "=> Toolchain selection"
	@echo "   I686_PREFIX = $(I686_PREFIX)"
	@for tool in $(AS) $(CC) $(CXX) $(LD) $(OBJCOPY) $(X64_CXX) $(X64_LD) $(X64_OBJCOPY) $(QEMU32) $(QEMU64); do \
		if ! command -v $$tool >/dev/null 2>&1; then \
			echo "Missing required tool: $$tool"; \
			echo ""; \
			echo "On Kali/Debian, install the expected tools with:"; \
			echo "  sudo apt-get update"; \
			echo '  sudo apt-get install -y nasm qemu-system-x86 clang lld llvm \\'; \
			echo '      gcc-i686-linux-gnu g++-i686-linux-gnu binutils-i686-linux-gnu'; \
			exit 1; \
		fi; \
		printf '   %-24s %s\n' $$tool "$$(command -v $$tool)"; \
	done

check-grub-tools:
	@for tool in grub-file grub-mkrescue xorriso; do \
		if ! command -v $$tool >/dev/null 2>&1; then \
			echo "Missing GRUB ISO tool: $$tool"; \
			echo ""; \
			echo "On Kali/Debian, install GRUB ISO tools with:"; \
			echo "  sudo apt-get update"; \
			echo "  sudo apt-get install -y grub-pc-bin xorriso"; \
			exit 1; \
		fi; \
		printf '   %-24s %s\n' $$tool "$$(command -v $$tool)"; \
	done

doctor: check-tools
	@echo "=> Disk image limits"
	@echo "   32-bit kernel: $(KERNEL_SECTORS_32) sectors / $(KERNEL_MAX_32) bytes"
	@echo "   64-bit kernel: $(KERNEL_SECTORS_64) sectors / $(KERNEL_MAX_64) bytes"

install-deps-help:
	@echo "Kali/Debian dependencies:"
	@echo "  sudo apt-get update"
	@echo '  sudo apt-get install -y nasm qemu-system-x86 clang lld llvm \'
	@echo '      gcc-i686-linux-gnu g++-i686-linux-gnu binutils-i686-linux-gnu'

run: run32

run32: all32
	@$(QEMU32) -drive file=$(IMAGE_FILE),format=raw,if=floppy -no-reboot -no-shutdown

run32-net: all32
	@$(QEMU32) -drive file=$(IMAGE_FILE),format=raw,if=floppy -netdev user,id=net0 -device rtl8139,netdev=net0 -serial stdio -no-reboot -no-shutdown

run64: all64
	@$(QEMU64) -drive file=$(IMAGE_FILE_64),format=raw,if=floppy -no-reboot -no-shutdown

run64-net: all64
	@$(QEMU64) -drive file=$(IMAGE_FILE_64),format=raw,if=floppy -netdev user,id=net0 -device rtl8139,netdev=net0 -serial stdio -no-reboot -no-shutdown

# Boot GRUB ISO with BIOS firmware (no UEFI)
run-grub: iso
	@$(QEMU64) -cdrom $(GRUB_ISO) -no-reboot -no-shutdown

# Boot GRUB ISO with UEFI firmware (OVMF). Requires ovmf package:
#   sudo apt-get install ovmf
run-uefi: iso
	@if [ -z "$(OVMF_FD)" ]; then \
		echo "ERROR: OVMF firmware not found. Install with: sudo apt-get install ovmf"; \
		exit 1; \
	fi
	@echo "=> Using OVMF: $(OVMF_FD)"
	@echo "=> Serial log: $(BUILD_DIR)/serial-uefi.log  (tail -f it in another terminal)"
	@$(QEMU64) -bios $(OVMF_FD) -cdrom $(GRUB_ISO) \
		-serial file:$(BUILD_DIR)/serial-uefi.log -no-reboot -no-shutdown

smoke: smoke32 smoke64

smoke32: all32
	@echo "=> Interactive boot smoke test: 32-bit"
	@(sleep 2; printf '$(SMOKE_COMMANDS)' | $(QEMU_SENDKEYS); sleep 2; echo 'screendump $(BUILD_DIR)/smoke32.ppm'; echo quit) | $(QEMU32) -drive file=$(IMAGE_FILE),format=raw,if=floppy -monitor stdio -display none -no-reboot -no-shutdown >/dev/null
	@test -s $(BUILD_DIR)/smoke32.ppm
	@echo "=> Wrote $(BUILD_DIR)/smoke32.ppm"

smoke64: all64
	@echo "=> Interactive boot smoke test: 64-bit"
	@(sleep 2; printf '$(SMOKE_COMMANDS)' | $(QEMU_SENDKEYS); sleep 2; echo 'screendump $(BUILD_DIR)/smoke64.ppm'; echo quit) | $(QEMU64) -drive file=$(IMAGE_FILE_64),format=raw,if=floppy -monitor stdio -display none -no-reboot -no-shutdown >/dev/null
	@test -s $(BUILD_DIR)/smoke64.ppm
	@echo "=> Wrote $(BUILD_DIR)/smoke64.ppm"

smoke32-net: all32
	@echo "=> Network boot smoke test: 32-bit RTL8139 + COM1"
	@rm -f $(BUILD_DIR)/tcp32-received.log
	@python3 scripts/tcp_smoke_server.py $(BUILD_DIR)/tcp32-received.log 8080 & server=$$!; \
	(sleep 2; printf 'netinfo\n' | $(QEMU_SENDKEYS); sleep 1; printf 'dhcp\n' | $(QEMU_SENDKEYS); sleep 12; printf 'dns example.com\n' | $(QEMU_SENDKEYS); sleep 3; printf 'arping 10.0.2.2\n' | $(QEMU_SENDKEYS); sleep 1; printf 'ping 10.0.2.2\n' | $(QEMU_SENDKEYS); sleep 2; printf 'udp 10.0.2.2 hello-from-mrhakos\n' | $(QEMU_SENDKEYS); sleep 1; printf 'tcp 10.0.2.2 8080 hello-tcp-from-mrhakos\n' | $(QEMU_SENDKEYS); sleep 4; printf 'netinfo\n' | $(QEMU_SENDKEYS); sleep 3; echo 'screendump $(BUILD_DIR)/smoke32-net.ppm'; echo quit) | $(QEMU32) -drive file=$(IMAGE_FILE),format=raw,if=floppy -netdev user,id=net0 -device rtl8139,netdev=net0 -serial file:$(BUILD_DIR)/serial32-net.log -monitor stdio -display none -no-reboot -no-shutdown >/dev/null; \
	wait $$server
	@test -s $(BUILD_DIR)/smoke32-net.ppm
	@test -s $(BUILD_DIR)/serial32-net.log
	@grep -q 'DHCPACK received' $(BUILD_DIR)/serial32-net.log
	@grep -q 'DNS A reply received' $(BUILD_DIR)/serial32-net.log
	@grep -q 'ICMP echo reply received' $(BUILD_DIR)/serial32-net.log
	@grep -q 'TCP SYN-ACK received' $(BUILD_DIR)/serial32-net.log
	@grep -q 'hello-tcp-from-mrhakos' $(BUILD_DIR)/tcp32-received.log
	@echo "=> Wrote $(BUILD_DIR)/smoke32-net.ppm and $(BUILD_DIR)/serial32-net.log"

smoke64-net: all64
	@echo "=> Network boot smoke test: 64-bit RTL8139 + COM1"
	@rm -f $(BUILD_DIR)/tcp64-received.log
	@python3 scripts/tcp_smoke_server.py $(BUILD_DIR)/tcp64-received.log 8080 & server=$$!; \
	(sleep 2; printf 'netinfo\n' | $(QEMU_SENDKEYS); sleep 1; printf 'dhcp\n' | $(QEMU_SENDKEYS); sleep 12; printf 'dns example.com\n' | $(QEMU_SENDKEYS); sleep 3; printf 'arping 10.0.2.2\n' | $(QEMU_SENDKEYS); sleep 1; printf 'ping 10.0.2.2\n' | $(QEMU_SENDKEYS); sleep 2; printf 'udp 10.0.2.2 hello-from-mrhakos\n' | $(QEMU_SENDKEYS); sleep 1; printf 'tcp 10.0.2.2 8080 hello-tcp-from-mrhakos64\n' | $(QEMU_SENDKEYS); sleep 4; printf 'netinfo\n' | $(QEMU_SENDKEYS); sleep 3; echo 'screendump $(BUILD_DIR)/smoke64-net.ppm'; echo quit) | $(QEMU64) -drive file=$(IMAGE_FILE_64),format=raw,if=floppy -netdev user,id=net0 -device rtl8139,netdev=net0 -serial file:$(BUILD_DIR)/serial64-net.log -monitor stdio -display none -no-reboot -no-shutdown >/dev/null; \
	wait $$server
	@test -s $(BUILD_DIR)/smoke64-net.ppm
	@test -s $(BUILD_DIR)/serial64-net.log
	@grep -q 'DHCPACK received' $(BUILD_DIR)/serial64-net.log
	@grep -q 'DNS A reply received' $(BUILD_DIR)/serial64-net.log
	@grep -q 'ICMP echo reply received' $(BUILD_DIR)/serial64-net.log
	@grep -q 'TCP SYN-ACK received' $(BUILD_DIR)/serial64-net.log
	@grep -q 'hello-tcp-from-mrhakos64' $(BUILD_DIR)/tcp64-received.log
	@echo "=> Wrote $(BUILD_DIR)/smoke64-net.ppm and $(BUILD_DIR)/serial64-net.log"

# Build a GRUB-bootable ISO for real hardware/USB testing. GRUB loads the
# 32-bit kernel ELF directly through Multiboot2; do not use Linux's `linux`
# command or try to chainload the raw floppy image from Kali GRUB.
iso: boot-report iso-checksum
	@echo "=> Real-hardware ISO ready: $(GRUB_ISO)"
	@echo "=> SHA256: $$(cut -d' ' -f1 $(GRUB_ISO_SHA))"

grubiso: $(KERNEL_ELF) check-grub-tools
	@echo "=> Checking Multiboot2 header in $(KERNEL_ELF)"
	@grub-file --is-x86-multiboot2 $(KERNEL_ELF)
	@rm -rf $(GRUB_ISODIR)
	@mkdir -p $(GRUB_ISODIR)/boot/grub
	@cp $(KERNEL_ELF) $(GRUB_ISODIR)/boot/mrhakos-kernel.elf
	@printf '%s\n' \
		'insmod all_video' \
		'insmod efi_gop' \
		'insmod video_bochs' \
		'insmod video_cirrus' \
		'set gfxmode=auto' \
		'set timeout=5' \
		'set default=0' \
		'' \
		'menuentry "MrHakOS 32-bit (Multiboot2)" {' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/mrhakos-kernel.elf' \
		'    boot' \
		'}' > $(GRUB_ISODIR)/boot/grub/grub.cfg
	@grub-mkrescue -o $(GRUB_ISO) $(GRUB_ISODIR) >/dev/null
	@echo "=> Wrote $(GRUB_ISO)"

iso-checksum: $(GRUB_ISO)
	@sha256sum $(GRUB_ISO) | tee $(GRUB_ISO_SHA)

boot-report: grubiso
	@echo "=> Verifying ISO hybrid boot metadata (BIOS MBR + GPT/EFI when GRUB tools provide it)"
	@file $(GRUB_ISO)
	@xorriso -indev $(GRUB_ISO) -report_system_area plain 2>/dev/null | tee $(BUILD_DIR)/boot-report.txt
	@grep -q 'MBR' $(BUILD_DIR)/boot-report.txt
	@grep -q 'GPT' $(BUILD_DIR)/boot-report.txt
	@echo "=> Boot report wrote $(BUILD_DIR)/boot-report.txt"

# Generate a standalone Kali /etc/grub.d script. Install manually with sudo
# after copying bin/kernel.elf to /boot/mrhakos/kernel.elf.
grub-menu-config: $(KERNEL_ELF)
	@mkdir -p $(BUILD_DIR)
	@printf '%s\n' \
		'#!/bin/sh' \
		'exec tail -n +3 $$0' \
		'' \
		'menuentry "MrHakOS 32-bit (Multiboot2)" {' \
		'    insmod multiboot2' \
		'    multiboot2 /mrhakos/kernel.elf' \
		'    boot' \
		'}' > $(GRUB_MENU_CFG)
	@echo "=> Wrote $(GRUB_MENU_CFG)"
	@echo "=> Copy $(KERNEL_ELF) to /boot/mrhakos/kernel.elf before using this menuentry."

grub-assets: $(KERNEL_ELF) grub-menu-config
	@grub-file --is-x86-multiboot2 $(KERNEL_ELF)
	@echo "=> GRUB assets ready:"
	@echo "   Kernel: $(KERNEL_ELF)"
	@echo "   Kali GRUB snippet: $(GRUB_MENU_CFG)"

$(IMAGE_FILE): $(BOOT_BIN) $(KERNEL_BIN) check-sizes32
	@echo "=> Creating final disk image: $@"
	@truncate -s $(FLOPPY_SIZE_BYTES) $@
	@dd if=$(BOOT_BIN) of=$@ bs=$(SECTOR_SIZE) count=1 conv=notrunc status=none
	@dd if=$(KERNEL_BIN) of=$@ bs=$(SECTOR_SIZE) seek=1 conv=notrunc status=none

# USB/raw-disk friendly alias with a .bin extension. This is the bootable
# artifact; bin/kernel.bin is only the flat kernel payload and cannot boot by
# itself because it intentionally does not contain the 512-byte boot sector.
$(BOOTABLE_BIN): $(IMAGE_FILE)
	@cp $< $@
	@echo "=> Wrote bootable raw image alias: $@"

$(IMAGE_FILE_64): $(BOOT64_BIN) $(KERNEL64_BIN) check-sizes64
	@echo "=> Creating 64-bit disk image: $@"
	@truncate -s $(FLOPPY_SIZE_BYTES) $@
	@dd if=$(BOOT64_BIN) of=$@ bs=$(SECTOR_SIZE) count=1 conv=notrunc status=none
	@dd if=$(KERNEL64_BIN) of=$@ bs=$(SECTOR_SIZE) seek=1 conv=notrunc status=none

check-sizes32: $(BOOT_BIN) $(KERNEL_BIN)
	@boot_size=$$(wc -c < $(BOOT_BIN)); \
	kernel_size=$$(wc -c < $(KERNEL_BIN)); \
	if [ $$boot_size -gt $(SECTOR_SIZE) ]; then \
		echo "ERROR: 32-bit bootloader is $$boot_size bytes; max is $(SECTOR_SIZE)"; \
		exit 1; \
	fi; \
	if [ $$kernel_size -gt $(KERNEL_MAX_32) ]; then \
		echo "ERROR: 32-bit kernel is $$kernel_size bytes; bootloader reads only $(KERNEL_SECTORS_32) sectors ($(KERNEL_MAX_32) bytes)"; \
		exit 1; \
	fi; \
	echo "=> Size check 32-bit: boot=$$boot_size bytes, kernel=$$kernel_size/$(KERNEL_MAX_32) bytes"

check-sizes64: $(BOOT64_BIN) $(KERNEL64_BIN)
	@boot_size=$$(wc -c < $(BOOT64_BIN)); \
	kernel_size=$$(wc -c < $(KERNEL64_BIN)); \
	if [ $$boot_size -gt $(SECTOR_SIZE) ]; then \
		echo "ERROR: 64-bit bootloader is $$boot_size bytes; max is $(SECTOR_SIZE)"; \
		exit 1; \
	fi; \
	if [ $$kernel_size -gt $(KERNEL_MAX_64) ]; then \
		echo "ERROR: 64-bit kernel is $$kernel_size bytes; bootloader reads only $(KERNEL_SECTORS_64) sectors ($(KERNEL_MAX_64) bytes)"; \
		exit 1; \
	fi; \
	echo "=> Size check 64-bit: boot=$$boot_size bytes, kernel=$$kernel_size/$(KERNEL_MAX_64) bytes"

$(KERNEL_ELF): $(OBJS)
	@echo "=> Linking kernel ELF: $@"
	@$(LD) $(LDFLAGS) -o $@ $^

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "=> Converting to flat binary: $@"
	@$(OBJCOPY) -O binary $< $@

$(KERNEL64_ELF): $(X64_OBJS)
	@echo "=> Linking 64-bit kernel ELF: $@"
	@$(X64_LD) $(X64_LDFLAGS) -o $@ $^

$(KERNEL64_BIN): $(KERNEL64_ELF)
	@echo "=> Converting 64-bit to flat binary: $@"
	@$(X64_OBJCOPY) -O binary $< $@

$(BUILD_DIR)/entry.o: $(KERNEL_ASM)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Assembling kernel entry: $<"
	@$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/entry64.o: $(KERNEL64_ASM)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Assembling 64-bit kernel entry: $<"
	@$(AS) -f elf64 $< -o $@

$(BUILD_DIR)/kernel.o: $(KERNEL_CPP)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling C++ kernel: $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel64.o: $(KERNEL_CPP)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling 64-bit C++ kernel: $<"
	@$(X64_CXX) $(X64_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/string.o: $(LIBC_CPP_STR)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (string): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/string64.o: $(LIBC_CPP_STR)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (string, x64): $<"
	@$(X64_CXX) $(X64_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/vga.o: $(LIBC_CPP_VGA)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (vga): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/vga64.o: $(LIBC_CPP_VGA)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (vga, x64): $<"
	@$(X64_CXX) $(X64_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/terminal.o: $(LIBC_CPP_TERM)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (terminal): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/terminal64.o: $(LIBC_CPP_TERM)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (terminal, x64): $<"
	@$(X64_CXX) $(X64_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts.o: $(LIBC_CPP_INT)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (interrupts): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts64_cpp.o: $(LIBC_CPP_INT)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (interrupts, x64): $<"
	@$(X64_CXX) $(X64_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: $(LIBC_CPP_IDT)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (idt): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/filesystem.o: $(LIBC_CPP_FS)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (filesystem): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/filesystem64.o: $(LIBC_CPP_FS)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (filesystem, x64): $<"
	@$(X64_CXX) $(X64_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/serial.o: $(LIBC_CPP_SERIAL)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (serial): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/serial64.o: $(LIBC_CPP_SERIAL)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (serial, x64): $<"
	@$(X64_CXX) $(X64_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/memory.o: $(LIBC_CPP_MEM)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (memory): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/memory64.o: $(LIBC_CPP_MEM)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (memory, x64): $<"
	@$(X64_CXX) $(X64_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/crypto.o: $(LIBC_CPP_CRYPTO)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (crypto): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/crypto64.o: $(LIBC_CPP_CRYPTO)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (crypto, x64): $<"
	@$(X64_CXX) $(X64_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/pci.o: $(LIBC_CPP_PCI)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (pci): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/pci64.o: $(LIBC_CPP_PCI)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (pci, x64): $<"
	@$(X64_CXX) $(X64_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/network.o: $(LIBC_CPP_NET)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (network): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/network64.o: $(LIBC_CPP_NET)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (network, x64): $<"
	@$(X64_CXX) $(X64_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/isr.o: $(LIBC_ASM_ISR)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Assembling 32-bit ISR stubs: $<"
	@$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/isr64.o: $(LIBC_ASM_ISR64)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Assembling 64-bit ISR stubs: $<"
	@$(AS) -f elf64 $< -o $@

$(BOOT_BIN): $(BOOT_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Assembling bootloader: $<"
	@$(AS) -DKERNEL_SECTORS=$(KERNEL_SECTORS_32) -f bin $< -o $@

$(BOOT64_BIN): $(BOOT64_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Assembling 64-bit bootloader: $<"
	@$(AS) -DKERNEL_SECTORS=$(KERNEL_SECTORS_64) -f bin $< -o $@

clean:
	@echo "=> Cleaning build directory"
	@rm -rf $(BUILD_DIR)
