# MrHakOS Makefile (Version 5 - 32-bit + 64-bit)

CC      = i686-elf-gcc
CXX     = i686-elf-g++
AS      = nasm
LD      = i686-elf-ld
OBJCOPY = i686-elf-objcopy

X64_CXX     = clang++
X64_LD      = ld.lld
X64_OBJCOPY = llvm-objcopy

BUILD_DIR = bin
SRC_DIR   = src

BOOT_SRC    = $(SRC_DIR)/boot/bootloader.asm
BOOT64_SRC  = $(SRC_DIR)/boot/bootloader64.asm
KERNEL_ASM  = $(SRC_DIR)/kernel/entry.asm
KERNEL64_ASM= $(SRC_DIR)/kernel/entry64.asm
KERNEL_CPP  = $(SRC_DIR)/kernel/kernel.cpp
KERNEL64_CPP= $(SRC_DIR)/kernel/kernel.cpp
KERNEL64_BIN_SRC = $(SRC_DIR)/kernel/kernel64.asm
LIBC_CPP_VGA   = $(SRC_DIR)/libc/vga.cpp
LIBC_CPP_STR   = $(SRC_DIR)/libc/string.cpp
LIBC_CPP_TERM  = $(SRC_DIR)/libc/terminal.cpp
LIBC_CPP_INT   = $(SRC_DIR)/libc/interrupts.cpp
LIBC_CPP_IDT   = $(SRC_DIR)/libc/idt.cpp
LIBC_CPP_FS    = $(SRC_DIR)/libc/filesystem.cpp

BOOT_BIN    = $(BUILD_DIR)/boot.bin
KERNEL_ELF  = $(BUILD_DIR)/kernel.elf
KERNEL_BIN  = $(BUILD_DIR)/kernel.bin
IMAGE_FILE  = $(BUILD_DIR)/mrhakos.img
BOOT64_BIN     = $(BUILD_DIR)/boot64.bin
KERNEL64_ELF   = $(BUILD_DIR)/kernel64.elf
KERNEL64_BIN   = $(BUILD_DIR)/kernel64.bin
IMAGE_FILE_64  = $(BUILD_DIR)/mrhakos64.img

OBJS = $(BUILD_DIR)/entry.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/string.o $(BUILD_DIR)/vga.o $(BUILD_DIR)/terminal.o $(BUILD_DIR)/interrupts.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/filesystem.o $(BUILD_DIR)/isr.o

ISR64_OBJ = $(BUILD_DIR)/isr64.o
X64_OBJS = $(BUILD_DIR)/entry64.o $(BUILD_DIR)/kernel64.o $(BUILD_DIR)/string64.o $(BUILD_DIR)/vga64.o $(BUILD_DIR)/terminal64.o $(BUILD_DIR)/interrupts64_cpp.o $(BUILD_DIR)/filesystem64.o $(ISR64_OBJ)
LIBC_ASM_ISR64 = $(SRC_DIR)/libc/isr64.asm
LIBC_ASM_ISR   = $(SRC_DIR)/libc/isr.asm

INCLUDES = -I$(SRC_DIR)/libc/include

CFLAGS   = -ffreestanding -O2 -nostdlib -Wall -Wextra $(INCLUDES)
CXXFLAGS = -ffreestanding -O2 -nostdlib -fno-exceptions -fno-rtti -Wall -Wextra $(INCLUDES)
LDFLAGS  = -T $(SRC_DIR)/kernel/linker.ld -nostdlib

.PHONY: all run all64 run64 clean

all: $(IMAGE_FILE)

all64: $(IMAGE_FILE_64)

run: all
	@qemu-system-i386 -fda $(IMAGE_FILE) -no-reboot -no-shutdown

run64: all64
	@qemu-system-x86_64 -fda $(IMAGE_FILE_64) -no-reboot -no-shutdown

$(IMAGE_FILE): $(BOOT_BIN) $(KERNEL_BIN)
	@echo "=> Creating final disk image: $@"
	@cat $^ > $@

$(IMAGE_FILE_64): $(BOOT64_BIN) $(KERNEL64_BIN)
	@echo "=> Creating 64-bit disk image: $@"
	@cat $^ > $@

$(KERNEL_ELF): $(OBJS)
	@echo "=> Linking kernel ELF: $@"
	@$(LD) $(LDFLAGS) -o $@ $^

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "=> Converting to flat binary: $@"
	@$(OBJCOPY) -O binary $< $@

$(KERNEL64_ELF): $(X64_OBJS)
	@echo "=> Linking 64-bit kernel ELF: $@"
	@$(X64_LD) -T $(SRC_DIR)/kernel/linker64.ld -nostdlib -o $@ $^

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

$(BUILD_DIR)/kernel64.o: $(KERNEL64_CPP)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling 64-bit C++ kernel: $<"
	@$(X64_CXX) -target x86_64-elf -D__x86_64__ -ffreestanding -O2 -nostdlib -fno-exceptions -fno-rtti -mno-red-zone -Wall -Wextra $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/string64.o: $(LIBC_CPP_STR)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (string, x64): $<"
	@$(X64_CXX) -target x86_64-elf -D__x86_64__ -ffreestanding -O2 -nostdlib -fno-exceptions -fno-rtti -mno-red-zone -Wall -Wextra $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/vga64.o: $(LIBC_CPP_VGA)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (vga, x64): $<"
	@$(X64_CXX) -target x86_64-elf -D__x86_64__ -ffreestanding -O2 -nostdlib -fno-exceptions -fno-rtti -mno-red-zone -Wall -Wextra $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/terminal64.o: $(LIBC_CPP_TERM)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (terminal, x64): $<"
	@$(X64_CXX) -target x86_64-elf -D__x86_64__ -ffreestanding -O2 -nostdlib -fno-exceptions -fno-rtti -mno-red-zone -Wall -Wextra $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/interrupts64_cpp.o: $(LIBC_CPP_INT)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (interrupts, x64): $<"
	@$(X64_CXX) -target x86_64-elf -D__x86_64__ -ffreestanding -O2 -nostdlib -fno-exceptions -fno-rtti -mno-red-zone -Wall -Wextra $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/interrupts64.o: $(LIBC_CPP_INT)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (interrupts, x64): $<"
	@$(X64_CXX) -target x86_64-elf -D__x86_64__ -ffreestanding -O2 -nostdlib -fno-exceptions -fno-rtti -mno-red-zone -Wall -Wextra $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/filesystem64.o: $(LIBC_CPP_FS)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (filesystem, x64): $<"
	@$(X64_CXX) -target x86_64-elf -D__x86_64__ -ffreestanding -O2 -nostdlib -fno-exceptions -fno-rtti -mno-red-zone -Wall -Wextra $(INCLUDES) -c $< -o $@

$(ISR64_OBJ): $(LIBC_ASM_ISR64)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Assembling 64-bit ISR stubs: $<"
	@$(AS) -f elf64 $< -o $@

$(BUILD_DIR)/isr.o: $(LIBC_ASM_ISR)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Assembling 32-bit ISR stubs: $<"
	@$(AS) -f elf32 $< -o $@

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

$(BUILD_DIR)/filesystem.o: $(LIBC_CPP_FS)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Compiling LibC C++ (filesystem): $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BOOT_BIN): $(BOOT_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Assembling bootloader: $<"
	@$(AS) -f bin $< -o $@

$(BOOT64_BIN): $(BOOT64_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "=> Assembling 64-bit bootloader: $<"
	@$(AS) -f bin $< -o $@

clean:
	@echo "=> Cleaning build directory"
	@rm -rf $(BUILD_DIR)