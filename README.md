# MrHakOS 🚀

> *"When I wrote this code—with a lot of help from AI—I understood it. Now, only God knows what's going on."*

![Code Understanding Meme](./image/meme.png)

A fully functional operating system written from scratch in C++ and Assembly, featuring both 32-bit and 64-bit kernels with a complete terminal interface, filesystem, and interrupt handling.

## ✨ Features

### Core Features
- **Dual Architecture Support**: Both 32-bit (Protected Mode) and 64-bit (Long Mode) kernels
- **Custom Bootloader**: BIOS-based bootloader with A20 line enabling and mode switching
- **VGA Text Mode**: Full-featured VGA driver with cursor control and scrolling
- **Keyboard Input**: PS/2 keyboard driver with interrupt-based input handling
- **Terminal Interface**: Interactive command-line terminal with command history

### Filesystem
- **In-Memory Filesystem**: Hierarchical directory structure
- **File Operations**:
  - `touch` - Create .hak files
  - `cat` - Read file contents
  - `echo` - Write to files using `>` redirection
  - `cp` - Copy files (supports directory paths!)
  - `mv` - Move/rename files and directories
  - `mkdir` - Create directories
  - `cd` - Navigate directories
  - `ls` - List directory contents

### System Commands
- `clear` - Clear the screen
- `help` - Display available commands
- `mrhakos` - Show OS information

### Technical Features
- **Interrupt Handling**: Custom IDT setup with keyboard ISR
- **Memory Management**: Custom `operator new/delete` implementations
- **String Library**: Freestanding C++ string operations
- **Stack Safety**: Static buffers to prevent stack overflow in 64-bit mode
- **Clean Build System**: Comprehensive Makefile for both architectures

## 🏗️ Architecture

### 32-bit (Protected Mode)
```
BIOS → Bootloader → Protected Mode → Kernel Entry → C++ Kernel → Terminal
```

### 64-bit (Long Mode)
```
BIOS → Bootloader → Protected Mode → PAE + Paging → Long Mode → Kernel Entry → C++ Kernel → Terminal
```

## 📁 Project Structure

```
MrHakOS/
├── Makefile              # Build system for both 32/64-bit
├── build_and_run.sh      # Quick build & run script (Linux/macOS)
├── build_and_run.bat     # Quick build & run script (Windows)
├── README.md             # This file
├── bin/                  # Build outputs
│   ├── mrhakos.img       # 32-bit OS image
│   └── mrhakos64.img     # 64-bit OS image
└── src/
    ├── boot/
    │   ├── bootloader.asm    # 32-bit bootloader
    │   └── bootloader64.asm  # 64-bit bootloader
    ├── kernel/
    │   ├── entry.asm         # 32-bit kernel entry
    │   ├── entry64.asm       # 64-bit kernel entry
    │   ├── kernel.cpp        # Main kernel code
    │   ├── linker.ld         # 32-bit linker script
    │   └── linker64.ld       # 64-bit linker script
    └── libc/
        ├── include/          # Header files
        │   ├── vga.hpp       # VGA driver
        │   ├── terminal.hpp  # Terminal interface
        │   ├── interrupts.hpp # Interrupt handling
        │   ├── filesystem.hpp # Filesystem
        │   ├── string.hpp    # String operations
        │   └── io.hpp        # I/O port operations
        ├── vga.cpp           # VGA implementation
        ├── terminal.cpp      # Terminal implementation
        ├── interrupts.cpp    # IDT and ISR setup
        ├── filesystem.cpp    # Filesystem implementation
        ├── string.cpp        # String library
        ├── idt.cpp           # IDT operations
        ├── isr.asm           # 32-bit ISR stubs
        └── isr64.asm         # 64-bit ISR stubs
```

## 🛠️ Prerequisites

### Required Tools

| Tool | Purpose | Installation |
|------|---------|--------------|
| **NASM** | Assembly compiler | [nasm.us](https://www.nasm.us/) |
| **i686-elf-gcc/g++** | 32-bit cross-compiler | Custom build or package manager |
| **clang++/lld** | 64-bit compiler | Package manager |
| **QEMU** | Emulator | [qemu.org](https://www.qemu.org/) |

### Installation

#### Linux (Debian/Ubuntu)
```bash
sudo apt-get update
sudo apt-get install nasm qemu-system-x86 clang lld build-essential
# For 32-bit: install i686-elf cross-compiler separately
```

#### macOS
```bash
brew install nasm qemu llvm
# For 32-bit: install i686-elf cross-compiler via Homebrew
```

#### Windows
1. Install NASM from [nasm.us](https://www.nasm.us/)
2. Install MinGW-w64 or MSYS2
3. Install QEMU from [qemu.org](https://www.qemu.org/)
4. Add all tools to your PATH

## 🚀 Quick Start

### Build and Run (32-bit)
```bash
make clean && make all && make run
```

### Build and Run (64-bit)
```bash
make clean && make all64 && make run64
```

### Using Scripts
```bash
# Linux/macOS
chmod +x build_and_run.sh
./build_and_run.sh

# Windows
build_and_run.bat
```

## 💻 Usage Examples

Once MrHakOS boots, you'll see:
```
Welcome MrHakOs Terminal
MrHakOS >>
```

Try these commands:
```bash
# Get help
help

# Create a directory
mkdir documents

# Create and write to a file
touch hello.hak
echo Hello, World! > hello.hak
cat hello.hak

# Copy file to directory
cp hello.hak documents/hello.hak

# Navigate and list
cd documents
ls
cat hello.hak

# Rename a file
mv hello.hak greeting.hak

# Back to root
cd /
ls
```

## 🔧 Technical Deep Dive

### Memory Layout
```
0x00000000 - 0x000003FF : Real Mode IVT (Interrupt Vector Table)
0x00000400 - 0x000004FF : BIOS Data Area
0x00000500 - 0x00007BFF : Free conventional memory
0x00007C00 - 0x00007DFF : Bootloader (512 bytes)
0x00010000 - 0x0001FFFF : Kernel loaded here (~64KB)
0x0009F000 - 0x0009FFFF : Stack (4KB)
0x000A0000 - 0x000BFFFF : VGA memory
0x000B8000 - 0x000B8FA0 : VGA text buffer (80x25)
```

### Key Implementation Details

#### 64-bit Stack Overflow Fix
Large stack-allocated arrays (512+ bytes) caused crashes in 64-bit mode. Solution: Use `static` buffers instead.
```cpp
// DON'T: Stack overflow in 64-bit
void processCommand(const char* cmd) {
    char command[256];  // Stack allocated
    char args[256];     // Stack allocated
}

// DO: Static buffers
void processCommand(const char* cmd) {
    static char command[256];  // Static - safe!
    static char args[256];
}
```

#### ISR Stack Alignment
x86-64 ABI requires 16-byte stack alignment before `call` instructions. The ISR naturally achieves this:
- CPU pushes 40 bytes (5×8) after interrupt
- ISR pushes 120 bytes (15×8) for registers
- Total: 160 bytes = 16-byte aligned ✓

## 🐛 Debugging Tips

### Debug Markers (Currently Enabled)
The codebase includes VGA debug markers showing system initialization progress. These appear as colored letters on screen during boot and can help diagnose crashes.

### QEMU Monitor
Press `Ctrl+Alt+2` in QEMU to access the monitor for debugging:
```
info registers  # View CPU registers
x /10i $rip     # Disassemble at instruction pointer
```

### Common Issues

| Issue | Solution |
|-------|----------|
| Black screen on boot | Check bootloader GDT descriptor (use `dq` for 64-bit) |
| Keyboard not working | Verify ISR name matches `extern "C"` declaration |
| Crash on command entry | Use static buffers, not stack arrays |
| Terminal not showing | Ensure VGA buffer is at `0xB8000` |

## 📚 Learning Resources

- [OSDev Wiki](https://wiki.osdev.org/) - Comprehensive OS development resource
- [Intel 64 and IA-32 Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [AMD64 Architecture Programmer's Manual](https://www.amd.com/en/support/tech-docs)

## 🎯 Future Enhancements

- [ ] User mode and system calls
- [ ] Process scheduling and multitasking
- [ ] Physical and virtual memory management
- [ ] Disk I/O and persistent filesystem
- [ ] Network stack
- [ ] Graphics mode support
- [ ] UEFI bootloader option

## 📜 License

This project is open source and available for educational purposes.

## 👨‍💻 Author

**Mohamed Hakkou (ImMrHak)**

---

*Built with ❤️, Assembly, C++, and a lot of debugging*

> **Remember**: If you can't explain your code 3 months later, that's a feature, not a bug! 😄