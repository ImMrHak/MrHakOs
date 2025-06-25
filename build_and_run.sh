#!/bin/bash

# MrHakOS build and run script for Linux

# Colors for output
GREEN="\033[0;32m"
YELLOW="\033[1;33m"
RED="\033[0;31m"
NC="\033[0m" # No Color

echo -e "${YELLOW}Building MrHakOS...${NC}"

# Check if required tools are installed
command -v nasm >/dev/null 2>&1 || { echo -e "${RED}Error: NASM is not installed. Please install it first.${NC}"; exit 1; }
command -v ld >/dev/null 2>&1 || { echo -e "${RED}Error: LD is not installed. Please install it first.${NC}"; exit 1; }
command -v qemu-system-i386 >/dev/null 2>&1 || { echo -e "${RED}Error: QEMU is not installed. Please install it first.${NC}"; exit 1; }

# Create bin directory if it doesn't exist
mkdir -p bin

# Compile bootloader
echo -e "${GREEN}Compiling bootloader...${NC}"
nasm -f bin -o bin/bootloader.bin src/boot/bootloader.asm
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to compile bootloader.${NC}"
    exit 1
fi

# Compile kernel entry
echo -e "${GREEN}Compiling kernel entry...${NC}"
nasm -f elf32 -o bin/entry.o src/kernel/entry.asm
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to compile kernel entry.${NC}"
    exit 1
fi

# Compile kernel
echo -e "${GREEN}Compiling kernel...${NC}"
nasm -f elf32 -o bin/kernel.o src/kernel/kernel.asm
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to compile kernel.${NC}"
    exit 1
fi

# Link kernel
echo -e "${GREEN}Linking kernel...${NC}"
ld -m elf_i386 -o bin/kernel.bin -Ttext 0x1000 bin/entry.o bin/kernel.o --oformat binary
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to link kernel.${NC}"
    exit 1
fi

# Create OS image
echo -e "${GREEN}Creating OS image...${NC}"
cat bin/bootloader.bin bin/kernel.bin > bin/os-image.bin
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to create OS image.${NC}"
    exit 1
fi

echo -e "${GREEN}Build completed successfully!${NC}"

# Run OS in QEMU
echo -e "${YELLOW}Running MrHakOS in QEMU...${NC}"
qemu-system-i386 -fda bin/os-image.bin

echo -e "${GREEN}Done!${NC}"