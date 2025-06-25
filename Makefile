# MrHakOS Makefile

# Compiler/Assembler settings
ASM=nasm
LD=ld
QEMU=qemu-system-i386

# Directories
BOOT_DIR=src/boot
KERNEL_DIR=src/kernel
BIN_DIR=bin

# Files
BOOTLOADER=$(BOOT_DIR)/bootloader.asm
KERNEL_ENTRY=$(KERNEL_DIR)/entry.asm
KERNEL=$(KERNEL_DIR)/kernel.asm

# Output files
BOOTLOADER_BIN=$(BIN_DIR)/bootloader.bin
KERNEL_ENTRY_OBJ=$(BIN_DIR)/entry.o
KERNEL_OBJ=$(BIN_DIR)/kernel.o
KERNEL_BIN=$(BIN_DIR)/kernel.bin
OS_IMAGE=$(BIN_DIR)/os-image.bin

# Default target
all: setup $(OS_IMAGE)

# Create necessary directories
setup:
	mkdir -p $(BIN_DIR)

# Build the bootloader
$(BOOTLOADER_BIN): $(BOOTLOADER)
	$(ASM) -f bin -o $@ $<

# Build the kernel entry
$(KERNEL_ENTRY_OBJ): $(KERNEL_ENTRY)
	$(ASM) -f elf32 -o $@ $<

# Build the kernel
$(KERNEL_OBJ): $(KERNEL)
	$(ASM) -f elf32 -o $@ $<

# Link the kernel
$(KERNEL_BIN): $(KERNEL_ENTRY_OBJ) $(KERNEL_OBJ)
	$(LD) -m elf_i386 -o $@ -Ttext 0x1000 $^ --oformat binary

# Build the final OS image
$(OS_IMAGE): $(BOOTLOADER_BIN) $(KERNEL_BIN)
	cat $^ > $@

# Run the OS in QEMU
run: $(OS_IMAGE)
	$(QEMU) -fda $<

# Clean up
clean:
	rm -rf $(BIN_DIR)/*

.PHONY: all setup run clean