# MrHakOS

A simple operating system that displays "WELCOME TO MrHakOS" on boot.

## Project Structure

```
.
├── Makefile           # Build instructions
├── README.md         # This file
├── build_and_run.bat # Windows build and run script
├── build_and_run.sh  # Linux/macOS build and run script
└── src/              # Source code
    ├── boot/         # Boot code
    │   └── bootloader.asm  # Bootloader assembly code
    └── kernel/       # Kernel code
        ├── entry.asm       # Kernel entry point
        ├── kernel.asm      # Main kernel code
        └── linker.ld       # Linker script for kernel
```

## Prerequisites

To build and run MrHakOS, you need the following tools:

- NASM (Netwide Assembler)
- LD (GNU Linker)
- objcopy (part of GNU Binutils, for Windows builds)
- QEMU (for emulation)

### Installing Prerequisites

#### On Windows:

1. Install NASM from: https://www.nasm.us/
2. Install MinGW (which includes LD) from: https://www.mingw-w64.org/
3. Install QEMU from: https://www.qemu.org/download/

Make sure to add all these tools to your PATH.

#### On Linux:

```bash
sudo apt-get update
sudo apt-get install nasm binutils qemu-system-x86 gcc-multilib
```

Note: `gcc-multilib` is required for 32-bit development on 64-bit systems.

#### On macOS:

```bash
brew install nasm binutils qemu
```

Note: On newer macOS versions, you might need additional configuration for 32-bit development. Consider using a cross-compilation toolchain or Docker if you encounter architecture compatibility issues.

## Building and Running the OS

### Using Makefile

To build the operating system using the Makefile, run:

```bash
make
```

This will create the necessary directories and build the OS image in the `bin` directory.

To run the OS in QEMU using the Makefile, use:

```bash
make run
```

### Using Scripts

Alternatively, you can use the provided scripts to build and run the OS:

#### On Linux/macOS:

```bash
# Make the script executable first
chmod +x build_and_run.sh
# Then run it
./build_and_run.sh
```

#### On Windows:

```cmd
build_and_run.bat
```

**Note for Windows Users:**

The Windows build script uses specific configurations to handle differences in the Windows toolchain:

1. It uses the `elf32` format for NASM instead of just `elf`
2. It uses a two-step process for creating the kernel binary:
   - First, it creates an ELF file using the Windows LD with `-m i386pe` flag
   - Then, it converts the ELF file to a flat binary using `objcopy`
3. It uses a linker script (`src/kernel/linker.ld`) to control the output format

This approach avoids the common error: `ld: cannot perform PE operations on non PE output file`.

If you encounter linking errors, make sure you have the correct version of binutils installed (which includes both LD and objcopy). Some versions of MinGW or MSYS2 may have compatibility issues with the linking process.

These scripts will compile the OS and automatically launch it in QEMU.

## How It Works

1. The bootloader (`src/boot/bootloader.asm`) is loaded by the BIOS at address 0x7C00.
2. The bootloader sets up the environment and displays the welcome message.
3. In a more complete OS, the bootloader would load the kernel, but this simple version just displays the message.

## Architecture Compatibility

MrHakOS is designed to run in 32-bit protected mode. All build scripts and the Makefile have been configured to ensure compatibility across different systems:

- On 64-bit Linux systems, we use `-f elf32` for NASM and `-m elf_i386` for LD
- On Windows, we use a two-step process with objcopy to handle PE format issues
- On macOS, additional configuration may be needed for 32-bit development

These settings ensure that the OS can be built consistently across different platforms.

## Extending the OS

To extend this OS, you might want to:

1. Modify the bootloader to load the kernel from disk
2. Implement more kernel functionality
3. Add support for keyboard input
4. Implement a simple file system

## Troubleshooting

### Common Issues

1. **Linking errors:**
   - Windows Error: `ld: cannot perform PE operations on non PE output file`
   - Solution: The updated Windows build script uses a two-step process with objcopy to avoid this error. Make sure both LD and objcopy are installed and in your PATH.
   
   - Linux Error: `ld: i386 architecture of input file is incompatible with i386:x86-64 output`
   - Solution: The updated Linux build script uses `-f elf32` for NASM and `-m elf_i386` for LD to ensure 32-bit compatibility on 64-bit systems.

2. **NASM format errors:**
   - Error: `nasm: error: unrecognized output format`
   - Solution: Different versions of NASM may support different output formats. Try using `elf32` instead of `elf` on Windows.

3. **QEMU not found:**
   - Error: `QEMU is not installed or not in PATH`
   - Solution: Make sure QEMU is installed and added to your system PATH.

4. **objcopy not found or errors:**
   - Error: `objcopy is not installed or not in PATH`
   - Solution: Make sure you have GNU Binutils installed, which includes objcopy. On Windows, this typically comes with MinGW or MSYS2.

5. **Kernel not loading:**
   - If you see the bootloader message but not the kernel message, there might be an issue with the kernel linking or loading process.
   - Check that the kernel binary is being correctly generated and appended to the OS image.

### Getting Help

If you encounter issues not covered here, you can:

1. Check the NASM, LD, and QEMU documentation for your specific platform
2. Look for OS development communities and forums online

## License

This project is open source and available for educational purposes.