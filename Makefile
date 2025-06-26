# MrHakOS Makefile (Version 4 - Simplified and Corrected)

# Toolchain
CC      = i686-elf-gcc
CXX     = i686-elf-g++
AS      = nasm
LD      = i686-elf-ld
OBJCOPY = i686-elf-objcopy

# Directories
BUILD_DIR = bin
SRC_DIR   = src

# Explicitly list all source files
BOOT_SRC    = $(SRC_DIR)/boot/bootloader.asm
KERNEL_ASM  = $(SRC_DIR)/kernel/entry.asm
KERNEL_CPP  = $(SRC_DIR)/kernel/kernel.cpp
LIBC_CPP_VGA   = $(SRC_DIR)/libc/vga.cpp
LIBC_CPP_STR   = $(SRC_DIR)/libc/string.cpp
LIBC_CPP_TERM  = $(SRC_DIR)/libc/terminal.cpp
LIBC_CPP_INT   = $(SRC_DIR)/libc/interrupts.cpp
LIBC_CPP_IDT   = $(SRC_DIR)/libc/idt.cpp

# Object files to be created in the build directory
BOOT_BIN    = $(BUILD_DIR)/boot.bin
KERNEL_ELF  = $(BUILD_DIR)/kernel.elf
KERNEL_BIN  = $(BUILD_DIR)/kernel.bin
IMAGE_FILE  = $(BUILD_DIR)/mrhakos.img

OBJS = $(BUILD_DIR)/entry.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/string.o $(BUILD_DIR)/vga.o $(BUILD_DIR)/terminal.o $(BUILD_DIR)/interrupts.o $(BUILD_DIR)/idt.o

# Include paths for our custom library
INCLUDES = -I$(SRC_DIR)/libc/include

# Flags
CFLAGS   = -ffreestanding -O2 -nostdlib -Wall -Wextra $(INCLUDES)
CXXFLAGS = -ffreestanding -O2 -nostdlib -fno-exceptions -fno-rtti -Wall -Wextra $(INCLUDES)
LDFLAGS  = -T $(SRC_DIR)/kernel/linker.ld -nostdlib

# --- Build Rules ---
.PHONY: all run clean

all: $(IMAGE_FILE)

run: all
	@qemu-system-i386 -fda $(IMAGE_FILE) -no-reboot -no-shutdown

$(IMAGE_FILE): $(BOOT_BIN) $(KERNEL_BIN)
	@echo "=> Creating final disk image: $@"
	@cat $^ > $@

$(KERNEL_ELF): $(OBJS)
	@echo "=> Linking kernel ELF: $@"
	@$(LD) $(LDFLAGS) -o $@ $^

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "=> Converting to flat binary: $@"
	@$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/entry.o: $(KERNEL_ASM)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Assembling kernel entry: $<"
	@$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/kernel.o: $(KERNEL_CPP)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling C++ kernel: $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/string.o: $(LIBC_CPP_STR)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (string): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/vga.o: $(LIBC_CPP_VGA)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (vga): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/terminal.o: $(LIBC_CPP_TERM)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (terminal): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts.o: $(LIBC_CPP_INT)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (interrupts): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: $(LIBC_CPP_IDT)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (idt): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BOOT_BIN): $(BOOT_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Assembling bootloader: $<"
	@$(AS) -f bin $< -o $@

clean:
	@echo "=> Cleaning build directory"
	@rm -rf $(BUILD_DIR)