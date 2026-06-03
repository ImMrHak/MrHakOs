# MrHakOS

A fully functional operating system written from scratch in C++ and Assembly, featuring both 32-bit (Protected Mode) and 64-bit (Long Mode) kernels, a complete terminal interface, an in-memory filesystem, interrupt handling, and a working TCP/IP networking stack.

---

## Table of Contents

1. [Features Overview](#features-overview)
2. [Architecture](#architecture)
3. [Project Structure](#project-structure)
4. [Source Files — Detailed Reference](#source-files--detailed-reference)
   - [Boot](#boot)
   - [Kernel Entry](#kernel-entry)
   - [Kernel Main](#kernel-main)
   - [Linker Scripts](#linker-scripts)
   - [VGA Driver](#vga-driver)
   - [Terminal](#terminal)
   - [Filesystem](#filesystem)
   - [Interrupts & IDT](#interrupts--idt)
   - [Serial Logging](#serial-logging)
   - [I/O Port Helpers](#io-port-helpers)
   - [PCI Bus](#pci-bus)
   - [Networking Stack](#networking-stack)
   - [String Library](#string-library)
   - [ISR Stubs (Assembly)](#isr-stubs-assembly)
5. [Build System](#build-system)
6. [Memory Layout](#memory-layout)
7. [Prerequisites & Installation](#prerequisites--installation)
8. [Quick Start](#quick-start)
9. [Terminal Commands Reference](#terminal-commands-reference)
10. [Networking Guide](#networking-guide)
11. [Real Hardware & GRUB Boot](#real-hardware--grub-boot)
12. [Smoke Tests](#smoke-tests)
13. [Scripts Reference](#scripts-reference)
14. [Technical Deep Dive](#technical-deep-dive)
15. [Debugging Tips](#debugging-tips)
16. [Future Roadmap](#future-roadmap)
17. [Learning Resources](#learning-resources)
18. [Author](#author)

---

## Features Overview

### Core OS
- **Dual architecture**: 32-bit (Protected Mode, IA-32) and 64-bit (Long Mode, x86-64) kernels from the same source tree
- **Custom BIOS bootloader**: loads and verifies the kernel before switching CPU modes
- **Freestanding C++ environment**: no hosted libc — every runtime dependency is implemented from scratch
- **Custom memory allocator**: bump-allocator-backed `operator new/delete` for kernel heap usage
- **Serial debug logging**: COM1 output for boot and runtime diagnostics without touching the screen

### VGA Display
- 80×25 text mode, memory-mapped at `0xB8000`
- Full color support (all 16 VGA foreground/background colors)
- Hardware cursor control via CRT I/O ports
- Automatic scrolling, line wrapping, and clear-screen

### Input & Interrupts
- PS/2 keyboard driver via IRQ1 interrupt with scan-code circular buffer
- Fallback **polling** of the i8042 data port for USB legacy keyboards (handles most real hardware laptops)
- PIC remapping (master 0x20–0x27, slave 0x28–0x2F)
- PIT timer at 1000 Hz for millisecond-accurate `pitSleepMs()` delays

### Terminal
- Interactive command-line prompt (`MrHakOS >> `)
- 20+ built-in commands (filesystem, system info, full networking suite)
- Backspace and character echo
- Keyboard input works through both IRQ1 and polled paths transparently

### Filesystem
- Hierarchical in-memory directory tree
- Full path navigation (`/`, `.`, `..`, relative paths)
- `.hak` file creation, read, write (via `echo >` redirection), copy, and move

### Networking
- PCI device scanner
- **RTL8139** driver (QEMU emulated NIC, 10/100 Mbps)
- **RTL8111/RTL8168/RTL8169** driver (Realtek Gigabit — real hardware descriptor-ring driver)
- Ethernet frame TX/RX
- ARP, IPv4 (static + DHCP), ICMP (ping + traceroute), UDP, DNS, TCP (client), HTTP GET

---

## Architecture

### 32-bit Boot Flow

```
BIOS
 └─► Bootloader (0x7C00)
      ├── Loads kernel sectors from disk via int 0x13
      ├── Verifies kernel signature (0x8664)
      ├── Sets up flat GDT (code + data segments)
      └─► Protected Mode jump → kernel at 0x10000
           └─► entry.asm → kernel_main() [C++]
                └─► VGA · Interrupts · FileSystem · Network · Terminal
```

### 64-bit Boot Flow

```
BIOS
 └─► Bootloader64 (0x7C00)
      ├── Loads kernel sectors from disk via int 0x13
      ├── Fast A20 enable (port 0x92)
      ├── Protected Mode (temporary)
      ├── Builds identity-map page tables (2MB page, 1:1)
      │    PML4 @ 0x90000 · PDPT @ 0x91000 · PD @ 0x92000
      ├── Enables PAE → sets EFER.LME → enables paging
      ├── Loads 64-bit GDT
      └─► Long Mode jump → kernel at 0x10008
           └─► entry64.asm → kernel_main() [C++]
                └─► VGA · Interrupts · FileSystem · Network · Terminal
```

---

## Project Structure

```
MrHakOS/
├── Makefile                        # Full build system (32/64-bit, QEMU, smoke tests, GRUB ISO)
├── README.md                       # This file
├── .gitignore                      # Ignores bin/*
├── image/
│   └── meme.png                    # Motivational image
├── scripts/
│   ├── bin/
│   │   └── tcp-received.log        # Output from TCP smoke test listener
│   ├── build_iso_usb.sh            # Build GRUB ISO and write to USB drive
│   ├── qemu_sendkeys.py            # Automate keyboard input into QEMU via HMP
│   ├── tcp_smoke_server.py         # Host-side TCP listener for smoke tests
│   └── udp_server.py               # Host-side UDP listener for manual testing
└── src/
    ├── boot/
    │   ├── bootloader.asm           # 32-bit BIOS bootloader
    │   └── bootloader64.asm         # 64-bit BIOS bootloader (includes paging setup)
    ├── kernel/
    │   ├── kernel.cpp               # Kernel main() — initializes all subsystems
    │   ├── entry.asm                # 32-bit assembly entry point (signature + stack + call)
    │   ├── entry64.asm              # 64-bit assembly entry point
    │   ├── linker.ld                # 32-bit ELF linker script (load at 0x10000)
    │   └── linker64.ld              # 64-bit ELF linker script (load at 0x10000)
    └── libc/
        ├── include/
        │   ├── vga.hpp              # VGA driver class declaration
        │   ├── terminal.hpp         # Terminal class + command declarations
        │   ├── filesystem.hpp       # FileSystem + Node structures + constants
        │   ├── interrupts.hpp       # IDT structures + global interrupt API
        │   ├── idt.hpp              # Raw IDT entry struct definitions (legacy)
        │   ├── serial.hpp           # Serial class (COM1 debug output)
        │   ├── io.hpp               # Inline inb/outb/inw/outw/inl/outl/io_wait
        │   ├── network.hpp          # Full networking stack class declaration
        │   ├── pci.hpp              # PCI scanner and config-space class
        │   └── string.hpp           # Freestanding strlen/strcmp/memset/memcpy
        ├── vga.cpp                  # VGA implementation
        ├── terminal.cpp             # Terminal command parsing and dispatch
        ├── filesystem.cpp           # In-memory filesystem implementation
        ├── interrupts.cpp           # IDT setup, PIC init, ISR registration, keyboard polling
        ├── idt.cpp                  # IDT load helper (legacy, minimal)
        ├── serial.cpp               # Serial COM1 implementation
        ├── pci.cpp                  # PCI bus enumeration and config-space access
        ├── network.cpp              # Complete networking stack implementation
        ├── string.cpp               # String library implementation
        ├── isr.asm                  # 32-bit ISR stubs (timer IRQ0, keyboard IRQ1)
        └── isr64.asm                # 64-bit ISR stubs (timer IRQ0, keyboard IRQ1)
```

---

## Source Files — Detailed Reference

### Boot

#### `src/boot/bootloader.asm` — 32-bit Bootloader
Located at physical address `0x7C00` (MBR), loaded by the BIOS on startup.

**Responsibilities:**
- Prints boot diagnostics via BIOS `int 0x10`
- Reads kernel sectors from disk using BIOS `int 0x13` (CHS mode, reads up to 96 sectors)
- Checks that the loaded kernel starts with the signature word `0x8664`
- Configures a minimal GDT with a flat 4GB code segment and flat 4GB data segment
- Disables interrupts, sets control register `CR0.PE`, and far-jumps into 32-bit Protected Mode
- Transfers control to the kernel entry point at physical address `0x10000`

#### `src/boot/bootloader64.asm` — 64-bit Bootloader
Same physical entry point (`0x7C00`), but the task is significantly more complex because x86-64 long mode requires paging to be active before the CPU can switch.

**Responsibilities:**
- Reads the same kernel sectors (loads to `0x10000`) with reduced diagnostic printing (space is tight in 512 bytes)
- Enables the A20 address line by writing bit 1 of port `0x92` (fast A20 method)
- Enters 32-bit Protected Mode temporarily
- Allocates minimal page tables in conventional memory:
  - PML4 table at `0x90000`
  - PDPT (Page Directory Pointer Table) at `0x91000`
  - PD (Page Directory) at `0x92000`
  - One 2MB huge page entry identity-maps the first 2MB of physical memory
- Sets `CR4.PAE` (Physical Address Extension), writes `EFER.LME` (Long Mode Enable) via `MSR 0xC0000080`, and enables paging with `CR0.PG`
- Loads a 64-bit GDT and far-jumps into 64-bit Long Mode
- Transfers control to the kernel at `0x10008` (skips the 8-byte signature header)

---

### Kernel Entry

#### `src/kernel/entry.asm` — 32-bit Entry
The first code the CPU runs after leaving the bootloader in 32-bit mode.

- Contains a `.signature` section at offset 0 holding the word `0x8664` (checked by the bootloader)
- Contains a valid **Multiboot2 header** so GRUB can load the kernel as an alternative to the custom bootloader
- Clears interrupt flag (`cli`), sets the stack pointer to `0x90000`, and calls `kernel_main()`
- Infinite halt loop if `kernel_main` ever returns

#### `src/kernel/entry64.asm` — 64-bit Entry
Same role as `entry.asm` but for 64-bit mode.

- Writes a test byte to VGA memory (`0xB8000`) as an early alive-signal
- Sets RSP to `0x9F000` (inside the 64KB stack area)
- Calls `kernel_main()`
- Infinite halt loop on return

---

### Kernel Main

#### `src/kernel/kernel.cpp`
The single C++ translation unit that owns `kernel_main()`. This function runs after the assembly entry stub and orchestrates the entire OS startup sequence:

```
1. Serial::init()         → enable COM1 debug output
2. Vga vga               → text mode 80×25 display
3. Interrupts interrupts  → IDT + PIC + PIT setup
4. FileSystem fs          → create root directory
5. Terminal terminal      → command interpreter object
6. Network g_network      → global network object
7. interrupts.init()      → install ISRs, remap PIC, start PIT timer
8. fs.init()              → initialize root filesystem node
9. g_network.init()       → scan PCI, find NIC, map registers
10. terminal.init(...)    → link all subsystems, probe DHCP
11. sti                   → enable CPU interrupts
12. terminal.run()        → enter the interactive command loop (never returns)
```

---

### Linker Scripts

#### `src/kernel/linker.ld` — 32-bit
Links the kernel ELF binary with load address `0x10000`. Defines four program headers: `signature` (read-only, must be the first 8 bytes so the bootloader's signature check works), `text` (executable code), `rodata` (constants), and `data` (writable data + BSS). Sections are 16-byte aligned.

#### `src/kernel/linker64.ld` — 64-bit
Same load address (`0x10000`) but produces a 64-bit ELF. Sections are 8-byte aligned. The `.signature` section is placed first for the same signature-check reason.

---

### VGA Driver

#### `src/libc/include/vga.hpp` + `src/libc/vga.cpp`
The VGA text-mode driver. The hardware is memory-mapped: each of the 2000 characters on the 80×25 grid is represented by a 16-bit value at `0xB8000 + (row * 80 + col) * 2`. The high byte is the color attribute; the low byte is the ASCII character.

**Key members:**

| Member | Description |
|---|---|
| `putChar(c, color)` | Write a single character with a given color at the current cursor position, advance cursor, handle newlines and line wrapping |
| `putString(s, color)` | Print a null-terminated string character by character |
| `clearScreen()` | Fill all 2000 cells with spaces in the default color |
| `scroll()` | Shift every row up by one, blank the last row |
| `setCursor(x, y)` | Move the hardware cursor by writing to CRT registers `0x3D4`/`0x3D5` |
| `enableCursor()` / `disableCursor()` | Toggle the blinking hardware cursor |

**Colors** are the standard 16 VGA palette entries (black, blue, green, cyan, red, magenta, brown, light grey, dark grey, light blue, light green, light cyan, light red, light magenta, yellow, white).

---

### Terminal

#### `src/libc/include/terminal.hpp` + `src/libc/terminal.cpp`
The interactive command interpreter. It owns the main run loop and dispatches user input to the correct handler.

**How input works:**
1. `run()` blocks on `getLastKey()` (the keyboard scan-code queue)
2. Printable characters are echoed to the screen and appended to a 256-byte line buffer
3. Backspace removes the last character from both the buffer and the display
4. Enter calls `processCommand()` which tokenizes the line and dispatches to a handler

**Command dispatch table:**

| Command | Handler | What it does |
|---|---|---|
| `help` | `cmdHelp()` | Print all available commands |
| `clear` | `cmdClear()` | Call `vga->clearScreen()` |
| `mrhakos` | `cmdMrHakOs()` | Display OS info banner |
| `mkdir <name>` | `cmdMkdir()` | Create a directory node under the current directory |
| `ls [path]` | `cmdLs()` | List directory entries, showing name, type (DIR/FILE), and size |
| `cd <path>` | `cmdCd()` | Navigate the directory tree; supports `/`, `..`, `.`, and relative names |
| `touch <name>` | `cmdTouch()` | Create an empty `.hak` file |
| `cat <name>` | `cmdCat()` | Print a file's content to the screen |
| `echo <text> > <file>` | `cmdEcho()` | Write text to a file (creates it if absent) |
| `cp <src> <dst>` | `cmdCp()` | Copy a file; destination can include a directory path |
| `mv <src> <dst>` | `cmdMv()` | Move or rename a file or directory |
| `netinfo` | `cmdNetinfo()` | Show NIC detection, link state, MAC, IP, RX/TX/protocol counters |
| `netpoll` | `cmdNetpoll()` | Manually drain the RTL8139 RX ring once |
| `arping <ip>` | `cmdArping()` | Broadcast ARP request; print resolved MAC |
| `ping <host>` | `cmdPing()` | ICMP echo (4 probes, TTL+RTT per reply, packet-loss statistics) |
| `dhcp` | `cmdDhcp()` | Run DHCP DISCOVER → OFFER → REQUEST → ACK; update IP/GW/DNS |
| `traceroute <host>` | `cmdTraceroute()` | ICMP route discovery; 3 probes per TTL from 1 to 30 |
| `udp <host> <text>` | `cmdUdp()` | Send a UDP datagram to port 9001 |
| `dns <hostname>` | `cmdDns()` | DNS A-record lookup via UDP port 53 |
| `tcp <host> <port> <text>` | `cmdTcp()` | Minimal TCP: SYN → SYN-ACK → ACK → PSH/ACK → FIN |
| `http <host>` | `cmdHttp()` | HTTP GET `/` on port 80; print first 512 bytes of response |
| `securechat <cmd>` | `cmdSecurechat()` | Placeholder — reserved for future onion-routed chat |
| `kbd` | `cmdKbd()` | Print IRQ1 vs. polled scancode counters (USB/legacy keyboard debug) |

---

### Filesystem

#### `src/libc/include/filesystem.hpp` + `src/libc/filesystem.cpp`
An in-memory hierarchical filesystem backed by a custom bump allocator. There is no disk I/O; everything lives in RAM and is lost on reboot.

**Internal structure:**

```cpp
struct Node {
    char name[64];          // Node name (file or directory)
    bool isDirectory;
    char* content;          // NULL for directories; points into content pool for files
    uint32_t contentSize;   // Bytes of content currently stored
    Node* children[32];     // Up to 32 child nodes per directory
    uint32_t childCount;
    Node* parent;           // Back-pointer for `cd ..`
};
```

**Hard limits:**

| Constraint | Value |
|---|---|
| Max name length | 64 characters |
| Max entries per directory | 32 |
| Max path depth | 8 levels |
| Max file content | 1 024 bytes per file |
| Content memory pool | 4 096 bytes total |

**Key operations:**

| Method | Description |
|---|---|
| `init()` | Create the root node `/` |
| `mkdir(name)` | Add a directory child to the current node |
| `touch(name)` | Add an empty file child with a `.hak` extension |
| `writeFile(name, content)` | Allocate from the content pool and write text |
| `readFile(name)` | Return a pointer to a file's content (read-only) |
| `cd(path)` | Walk the path string and update `currentNode`; handles `..`, `/`, relative |
| `ls(path)` | Iterate children of the target directory and return metadata |
| `cp(src, dst)` | Locate src file, allocate new content, create dst file in target dir |
| `mv(src, dst)` | Rename or reparent a node (file or directory) |

**Memory management:** `operator new` is implemented as a bump allocator pointing into a static 64 KB pool. `operator delete` is a no-op (no fragmentation in a kernel without a heap allocator).

---

### Interrupts & IDT

#### `src/libc/include/interrupts.hpp` + `src/libc/interrupts.cpp`

This component owns the **Interrupt Descriptor Table** (IDT), **PIC** remapping, **PIT** timer, and the **PS/2 keyboard** driver.

**IDT layout:**
- 256 entries
- 32-bit mode: 8 bytes per entry (segment selector + offset + type flags)
- 64-bit mode: 16 bytes per entry (adds the upper 32 bits of the 64-bit handler address)

**PIC remapping:**
The BIOS leaves the 8259 PIC with IRQ0–IRQ7 mapped to interrupt vectors 8–15, which collides with CPU exception vectors. `init()` remaps:
- Master PIC → vectors 0x20–0x27
- Slave PIC → vectors 0x28–0x2F

**PIT (Programmable Interval Timer):**
Configured in mode 3 (square wave) with a divisor of 1193 (= 1193182 Hz / 1000), giving a 1 ms tick rate. IRQ0 increments `timerTicks` and `timerMillis` on each tick.

**Keyboard (IRQ1):**
The ISR reads the scan code from port `0x60` and pushes it into a 64-byte circular queue. Extended codes (`0xE0` prefix) and shift keys (`0x2A`/`0x36` press, `0xAA`/`0xB6` release) are tracked so shifted characters translate correctly.

**USB legacy fallback (polling):**
`pollKeyboard()` reads port `0x60` directly when the i8042 status register (port `0x64`) signals data ready. This catches keystrokes from USB keyboards that firmware exposes through "USB Legacy Support" without firing IRQ1.

**Global API (used by `terminal.cpp`):**

```cpp
char     getLastKey();                  // Dequeue next character (blocking-style)
bool     pollKeyboard();                // Poll i8042 for USB/legacy input
uint32_t keyboardIrqScancodes();        // Total scancodes received via IRQ1
uint32_t keyboardPolledScancodes();     // Total scancodes received via polling
uint32_t timerTicks();                  // Raw PIT tick counter
uint32_t timerMillis();                 // Milliseconds since boot
void     pitSleepMs(uint32_t ms);       // Busy-wait for N milliseconds
```

#### `src/libc/include/idt.hpp` + `src/libc/idt.cpp`
Legacy helpers that define the raw IDT entry structure and the `lidt` instruction wrapper. Most of the real work now lives in `interrupts.cpp`.

---

### Serial Logging

#### `src/libc/include/serial.hpp` + `src/libc/serial.cpp`
Sends debug text over the **COM1** UART (`0x3F8`) so you can read boot and runtime logs without them appearing on the VGA screen. Useful when QEMU is run with `-serial stdio` or `-serial file:log.txt`.

**Configuration:** 38400 baud (divisor 3), 8-N-1 (8 data bits, no parity, 1 stop bit), FIFO enabled.

**API:**

```cpp
static void init();            // Configure UART registers
static void putChar(char c);   // Transmit one byte (waits for FIFO room)
static void print(const char* s);
static void printHex8(uint8_t v);
static void printHex16(uint16_t v);
static void printHex32(uint32_t v);
```

Newlines are automatically converted to CR+LF (`\r\n`) to satisfy terminal emulators.

---

### I/O Port Helpers

#### `src/libc/include/io.hpp`
A header-only set of inline functions wrapping x86 `in`/`out` instructions. Used throughout the kernel wherever hardware register access is needed (PIC, PIT, keyboard, VGA, serial, PCI, NIC).

```cpp
void     outb(uint16_t port, uint8_t  value);
void     outw(uint16_t port, uint16_t value);
void     outl(uint16_t port, uint32_t value);
uint8_t  inb (uint16_t port);
uint16_t inw (uint16_t port);
uint32_t inl (uint16_t port);
void     io_wait();    // write to port 0x80 — introduces a ~1µs delay after fast port writes
```

---

### PCI Bus

#### `src/libc/include/pci.hpp` + `src/libc/pci.cpp`
PCI configuration space access via the standard mechanism: write the address to port `0xCF8`, read/write data via port `0xCFC`.

**Device enumeration:**
`scan()` iterates all 256 buses × 32 devices × 8 functions and collects `PciDevice` records for every slot that returns a valid vendor ID (not `0xFFFF`).

**Lookup methods:**

```cpp
PciDevice* findDevice(uint16_t vendorId, uint16_t deviceId);
PciDevice* findClass(uint8_t classCode, uint8_t subclass);
```

**Config-space I/O:**

```cpp
uint8_t  readConfig8 (uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);
uint16_t readConfig16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);
uint32_t readConfig32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);
void     writeConfig16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint16_t val);
void     writeConfig32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val);
```

Each `PciDevice` record stores: vendor ID, device ID, class code, subclass, revision, IRQ line, and all 6 BARs (Base Address Registers — used to find the NIC's I/O or MMIO base).

---

### Networking Stack

#### `src/libc/include/network.hpp` + `src/libc/network.cpp`
The most complex component — a complete userspace-style networking stack implemented inside the kernel.

#### Supported NICs

| Driver | Hardware | Mode |
|---|---|---|
| RTL8139 | QEMU emulated (`10ec:8139`) | Polled TX/RX |
| RTL8169/RTL8168/RTL8111 | Real Realtek Gigabit cards | Descriptor-ring, polled |

The 64-bit RTL8169 driver uses 16 RX descriptors (2 KB buffers each) and 4 TX descriptors, accessing registers via the PCI I/O BAR.

#### Protocol Stack

```
Application layer:   HTTP GET / · DNS A-record · TCP client text · UDP text
Transport layer:     TCP (SYN/SYN-ACK/ACK/PSH-ACK/FIN) · UDP
Network layer:       IPv4 (static + DHCP) · ICMP (echo request/reply + time exceeded)
Link layer:          ARP (request + reply, single-entry cache) · Ethernet II frames
Hardware:            RTL8139 (QEMU) · RTL8111/8168/8169 (real hardware)
```

#### Key Network Methods

| Method | Protocol | Description |
|---|---|---|
| `init()` | — | Scan PCI, detect NIC, read MAC, set default IP/GW/DNS |
| `sendEthernet(dst, type, payload, len)` | Ethernet | Build and transmit a raw Ethernet frame |
| `receiveEthernet()` | Ethernet | Poll RX queue; dispatch ARP/IPv4 frames |
| `sendArp(targetIp)` | ARP | Broadcast ARP request; return resolved MAC |
| `sendIcmpEcho(dstIp, seq)` | ICMP | Transmit echo-request and wait for reply |
| `ping(host)` | ICMP | 4-probe ping with RTT and statistics |
| `traceroute(host)` | ICMP | TTL 1–30, 3 probes each, report intermediate hops |
| `sendUdp(dstIp, dstPort, data, len)` | UDP | Transmit one UDP datagram |
| `dnsLookup(hostname)` | DNS | UDP query to DNS server; parse A-record answer |
| `sendTcp(host, port, data)` | TCP | Full client session: SYN→SYN-ACK→ACK→PSH-ACK→FIN |
| `httpGet(host)` | HTTP | `GET / HTTP/1.0` over TCP; return first 512 bytes |
| `dhcpDiscover()` | DHCP | Full DISCOVER→OFFER→REQUEST→ACK state machine |
| `getNetinfo()` | — | Return struct with MAC, IP, GW, link state, counters |

#### Default QEMU Network Configuration

| Parameter | Value |
|---|---|
| MrHakOS IP | `10.0.2.15` |
| Gateway | `10.0.2.2` |
| DNS server | `10.0.2.3` |
| Broadcast | `10.0.2.255` |
| NIC (QEMU) | RTL8139, MAC `52:54:00:12:34:56` |

Running `dhcp` inside the OS can auto-configure these same values from QEMU's built-in DHCP server.

---

### String Library

#### `src/libc/include/string.hpp` + `src/libc/string.cpp`
A minimal freestanding implementation of the C string functions the kernel needs. Written without any reliance on the hosted C library.

```cpp
size_t strlen (const char* s);
int    strcmp (const char* a, const char* b);
void*  memset (void* dst, int c, size_t n);
void*  memcpy (void* dst, const void* src, size_t n);
```

---

### ISR Stubs (Assembly)

#### `src/libc/isr.asm` — 32-bit ISR stubs
Defines the interrupt service routine entry points that the CPU jumps to when IRQ0 (timer) or IRQ1 (keyboard) fires.

Each stub:
1. Saves all general-purpose registers (`pusha`)
2. Saves segment registers and sets up kernel data segment
3. Calls the C++ handler (`timer_handler` or `keyboard_handler`)
4. Sends EOI (End Of Interrupt, `0x20`) to the PIC
5. Restores registers and returns with `iret`

#### `src/libc/isr64.asm` — 64-bit ISR stubs
Same purpose, but for 64-bit mode. Pushes all 15 general-purpose registers manually (no `pusha` in 64-bit mode), maintains 16-byte stack alignment per the x86-64 ABI (the CPU pushes 5×8 = 40 bytes; the stub pushes 15×8 = 120 bytes; total 160 bytes is 16-byte aligned), and uses `iretq` (64-bit return from interrupt).

---

## Build System

The `Makefile` is the single source of truth for compilation, linking, and testing. It auto-detects whether the `i686-elf-*` cross-compiler or the `i686-linux-gnu-*` system package is installed and selects accordingly.

### Make Targets

| Target | Description |
|---|---|
| `make all32` | Compile and link the 32-bit kernel; produce bootable `bin/mrhakos.img` and `bin/mrhakos-bootable.bin` |
| `make all64` | Compile and link the 64-bit kernel; produce bootable `bin/mrhakos64.img` |
| `make run32` | Build (if needed) + run 32-bit in QEMU, no network |
| `make run64` | Build (if needed) + run 64-bit in QEMU, no network |
| `make run32-net` | 32-bit QEMU with RTL8139 NIC + user networking (NAT) |
| `make run64-net` | 64-bit QEMU with RTL8139 NIC + user networking (NAT) — **recommended for networking** |
| `make smoke` | Headless boot of both images; type test commands; save screenshots to `bin/smoke32.ppm` and `bin/smoke64.ppm` |
| `make smoke32` / `make smoke64` | Headless single-arch smoke test |
| `make smoke32-net` / `make smoke64-net` | Headless smoke test with RTL8139 NIC; captures serial log + TCP payload |
| `make grubiso` | Build a GRUB2-bootable ISO image at `bin/mrhakos-grub.iso` |
| `make grub-assets` | Generate `bin/kernel.elf` + `bin/41_mrhakos` GRUB script |
| `make clean` | Remove all build artifacts from `bin/` |
| `make doctor` | Check for required tools and print their versions |
| `make install-deps-help` | Print the exact `apt-get` install command for Kali/Debian/Ubuntu |

### Compiler Flags

```makefile
# 32-bit C++
i686-elf-g++ -ffreestanding -Os -nostdlib -fno-exceptions -fno-rtti -fcheck-new -Wall -Wextra

# 64-bit C++
clang++ -target x86_64-elf -ffreestanding -Os -nostdlib -fno-exceptions -fno-rtti \
        -mno-red-zone -mgeneral-regs-only -D__x86_64__

# Assembly
nasm -f elf32   (32-bit)
nasm -f elf64   (64-bit)
```

**`-mno-red-zone`** is critical for 64-bit: without it the ABI reserves 128 bytes below RSP ("red zone"), which an interrupt could silently clobber between instructions.  
**`-mgeneral-regs-only`** prevents the compiler from emitting SSE/AVX instructions, which require saving XMM registers across interrupts — an extra burden not worth taking on in a kernel without a user mode yet.

---

## Memory Layout

```
Physical Address         Contents
────────────────────────────────────────────────────────────
0x00000000 – 0x000003FF  Real Mode Interrupt Vector Table (IVT)
0x00000400 – 0x000004FF  BIOS Data Area (BDA)
0x00000500 – 0x00007BFF  Free conventional memory
0x00007C00 – 0x00007DFF  Bootloader (512 bytes, MBR position)
0x00007E00 – 0x0000FFFF  Additional conventional memory
0x00010000 – 0x0001FFFF  Kernel image (~64 KB max)
0x00090000 – 0x00090FFF  64-bit PML4 page table
0x00091000 – 0x00091FFF  64-bit PDPT page table
0x00092000 – 0x00092FFF  64-bit PD page table
0x0009F000 – 0x0009FFFF  Kernel stack (4 KB)
0x000A0000 – 0x000AFFFF  VGA memory (general)
0x000B8000 – 0x000B8F9F  VGA text buffer (80×25 = 4000 bytes)
```

The 32-bit kernel stack is at `0x90000`; the 64-bit kernel stack is at `0x9F000`. Both grow downward. The page tables for 64-bit mode are placed between them (`0x90000`–`0x92FFF`) and are never overwritten by the stack since the 64-bit stack is set to `0x9F000`.

---

## Prerequisites & Installation

### Required Tools

| Tool | Purpose | Notes |
|---|---|---|
| **NASM** | Assembles `.asm` source files | Any modern version |
| **i686-elf-g++** or **i686-linux-gnu-g++** | Compiles 32-bit freestanding C++ | Makefile prefers `i686-elf-*`; falls back to `i686-linux-gnu-*` |
| **clang++ / lld / llvm-objcopy** | Compiles and links 64-bit kernel | LLVM ≥ 12 recommended |
| **QEMU (qemu-system-x86_64)** | Emulates the OS | Required for `run*` and `smoke*` targets |
| **Python 3** | Drives smoke test keyboard input and host-side TCP/UDP listeners | Only needed for smoke tests |
| **grub-pc-bin / grub-common / xorriso** | Builds bootable ISO | Only needed for `grubiso` target |

### Kali / Debian / Ubuntu

```bash
sudo apt-get update
sudo apt-get install -y \
    nasm qemu-system-x86 \
    clang lld llvm \
    gcc-i686-linux-gnu g++-i686-linux-gnu binutils-i686-linux-gnu \
    grub-pc-bin grub-common xorriso python3
```

Run `make doctor` to verify all tools are found before building.

### macOS

```bash
brew install nasm qemu llvm
# For the 32-bit cross-compiler, build i686-elf-gcc from source
# or use a Homebrew tap such as nativeos/i686-elf-toolchain
```

### Windows

1. Install [NASM](https://www.nasm.us/)
2. Install [MSYS2](https://www.msys2.org/) and use its package manager for `clang`, `llvm`, `nasm`, and MinGW
3. Install [QEMU for Windows](https://www.qemu.org/)
4. Add all tool directories to your `PATH`

---

## Quick Start

```bash
# 32-bit — build and run
make clean && make all32 && make run32

# Important output files:
#   bin/kernel.bin            = raw kernel only, NOT directly bootable
#   bin/mrhakos.img           = bootable raw floppy/disk image
#   bin/mrhakos-bootable.bin  = same bootable raw image, .bin extension

# 64-bit — build and run
make clean && make all64 && make run64

# 64-bit with networking (recommended for network commands)
make run64-net

# 32-bit with networking
make run32-net
```

When QEMU opens you will see the MrHakOS banner followed by the prompt:

```
Welcome MrHakOs Terminal
MrHakOS >>
```

---

## Terminal Commands Reference

### Filesystem Commands

```bash
mkdir documents               # Create directory 'documents' under current path
ls                            # List current directory (name, type, size)
ls /documents                 # List a specific path
cd documents                  # Enter directory
cd ..                         # Go up one level
cd /                          # Jump to root
touch hello.hak               # Create empty file
echo Hello World > hello.hak  # Write text to file
cat hello.hak                 # Print file contents
cp hello.hak documents/copy.hak  # Copy file into a directory
mv hello.hak renamed.hak      # Rename file
mv documents archive          # Rename directory
```

### System Commands

```bash
help        # List all commands
clear       # Clear screen
mrhakos     # Show OS version banner
kbd         # Show IRQ1 vs. polled scancode counters (keyboard debug)
```

### Network Commands

```bash
netinfo                           # NIC type, link state, MAC, IP, counters
dhcp                              # Auto-configure IP/GW/DNS from DHCP server
arping 10.0.2.2                   # Resolve MAC of a given IP via ARP
ping 10.0.2.2                     # 4 ICMP echo probes with RTT and stats
ping example.com                  # Hostname resolved via DNS first
traceroute 10.0.2.2               # ICMP TTL-probe route discovery
udp 10.0.2.2 hello-from-mrhakos  # Send text via UDP to port 9001
dns example.com                   # DNS A-record lookup
tcp 10.0.2.2 8080 hello           # TCP client: send text and close
http 1.1.1.1                      # HTTP GET / on port 80, print response
netpoll                           # Manually drain the NIC RX ring once
```

---

## Networking Guide

### QEMU User Networking (NAT)

Use `make run32-net` or `make run64-net`. QEMU provides a private NAT network:

```
MrHakOS  10.0.2.15  ──►  QEMU NAT (10.0.2.2)  ──►  Host Internet
                         DNS: 10.0.2.3
```

Important caveat: QEMU user-mode networking (SLIRP) does **not** forward raw ICMP. This means `ping 1.1.1.1` will not receive a reply even though the host can ping that address. Use `http 1.1.1.1` or `tcp PUBLIC_IP 80 hello` to test public connectivity.

### Step-by-step networking walkthrough

```bash
# 1. Check the NIC was detected
netinfo

# 2. (Optional) get IP from QEMU's built-in DHCP server
dhcp

# 3. Verify new IP
netinfo

# 4. ARP resolve the gateway
arping 10.0.2.2

# 5. Ping the gateway (works through user-mode NAT)
ping 10.0.2.2

# 6. Trace route
traceroute 10.0.2.2

# 7. DNS lookup
dns example.com

# 8. HTTP to a public server (TCP + HTTP over NAT — works!)
http 1.1.1.1
http example.com
```

### Testing UDP

On the host, before starting QEMU:

```bash
python3 scripts/udp_server.py
# or manually:
python3 -c "
import socket; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
s.bind(('0.0.0.0',9001)); print('UDP 9001 ready'); print(s.recvfrom(2048))"
```

Inside MrHakOS:

```bash
udp 10.0.2.2 hello-from-mrhakos
```

### Testing TCP

On the host, before starting QEMU:

```bash
python3 scripts/tcp_smoke_server.py
# or manually:
python3 -c "
import socket; s=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(('0.0.0.0',8080))
s.listen(1); print('TCP 8080 ready'); c,a=s.accept(); print(a,c.recv(2048)); c.close()"
```

Inside MrHakOS:

```bash
tcp 10.0.2.2 8080 hello-tcp-from-mrhakos
```

### Expected `netinfo` output (QEMU)

```
NIC: RTL8139
MAC: 52:54:00:12:34:56
IP:  10.0.2.15
GW:  10.0.2.2
DNS: 10.0.2.3
Link: UP
RX: 4 packets
TX: 6 packets
ARP: 1 packets
IPv4: 3 packets
ICMP: 1 packets
```

---

## Real Hardware & GRUB Boot

### Laptop / desktop with a Realtek Gigabit NIC

MrHakOS includes a descriptor-ring driver for RTL8111/RTL8168/RTL8169 chipsets (PCI vendor `10ec`, device `8168`). The driver is polled (not interrupt-driven). It reads the MAC address from the NIC, detects link state, and handles DHCP.

Boot using GRUB Multiboot2 (see below). Then inside MrHakOS:

```bash
netinfo   # should show RTL8169, MAC, and link UP
dhcp
ping 192.168.1.1   # your router
```

If `netinfo` shows `Unsupported Ethernet device`, the machine uses a NIC family not yet implemented (Intel e1000, Broadcom, etc.).

### Booting with GRUB (Multiboot2 — for existing Kali/Linux install)

```bash
# Build kernel ELF + GRUB menu script
make clean && make all32 && make grub-assets

# Install on the running system
sudo mkdir -p /boot/mrhakos
sudo cp bin/kernel.elf /boot/mrhakos/kernel.elf
sudo cp bin/41_mrhakos /etc/grub.d/41_mrhakos
sudo chmod +x /etc/grub.d/41_mrhakos
sudo update-grub
```

The GRUB entry added to `/etc/grub.d/41_mrhakos`:

```grub
menuentry "MrHakOS 32-bit (Multiboot2)" {
    insmod multiboot2
    multiboot2 /mrhakos/kernel.elf
    boot
}
```

Reboot and select **MrHakOS 32-bit (Multiboot2)** from the GRUB menu.

### Bootable USB/ISO

For real USB boot on normal PCs, the safest path is the GRUB ISO:

```bash
make grubiso
# writes bin/mrhakos-grub.iso
# then use scripts/build_iso_usb.sh or dd manually:
sudo dd if=bin/mrhakos-grub.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

The custom BIOS raw image is also bootable and now has a `.bin` alias if you specifically want a bootable `.bin` file:

```bash
make all32
sudo dd if=bin/mrhakos-bootable.bin of=/dev/sdX bs=4M status=progress conv=fsync
```

Do not write `bin/kernel.bin` to USB by itself: it is only the kernel payload and intentionally has no `0x55AA` boot-sector signature. Use `bin/mrhakos-grub.iso`, `bin/mrhakos.img`, or `bin/mrhakos-bootable.bin` instead.

Use `scripts/build_iso_usb.sh` for an interactive script with safety checks (verifies you are targeting a whole disk, not a partition, and prompts for confirmation).

### USB Keyboard on Real Hardware

If the keyboard is unresponsive after GRUB boot, MrHakOS has a fallback polling path. Type `kbd` to check which path is active:

```
IRQ1 scancodes:  0       ← PS/2 interrupt path (not working)
Polled scancodes: 42     ← USB legacy fallback (working!)
```

If both are 0, enter your BIOS/UEFI setup and enable **USB Legacy Support**, **Legacy USB Keyboard**, or **CSM/Legacy Boot**.

---

## Smoke Tests

Smoke tests boot the OS headlessly in QEMU, automate keyboard input, and verify the output via screenshots and serial logs.

```bash
make smoke            # Both 32-bit and 64-bit, no networking
make smoke32          # 32-bit only
make smoke64          # 64-bit only
make smoke32-net      # 32-bit + RTL8139 networking
make smoke64-net      # 64-bit + RTL8139 networking (most comprehensive)
```

**What the basic smoke test does:**
1. Boots the kernel
2. Types: `help`, `mkdir docs`, `touch hello.hak`, `echo hello > hello.hak`, `cat hello.hak`, `ls`
3. Saves a screenshot to `bin/smoke32.ppm` / `bin/smoke64.ppm`

**What the network smoke test adds:**
1. Types: `netinfo`, `dns example.com`, `arping 10.0.2.2`, `ping 10.0.2.2`, `udp 10.0.2.2 hello-from-mrhakos`, `tcp 10.0.2.2 8080 hello-tcp-from-mrhakos`, `netinfo`
2. Starts a host TCP listener (`scripts/tcp_smoke_server.py`) and verifies the received payload
3. Captures COM1 serial output to `bin/serial32-net.log` / `bin/serial64-net.log`
4. Saves network screenshot to `bin/smoke32-net.ppm` / `bin/smoke64-net.ppm`
5. Saves TCP received data to `bin/tcp32-received.log` / `bin/tcp64-received.log`

---

## Scripts Reference

### `scripts/qemu_sendkeys.py`

Converts text from stdin into a sequence of QEMU HMP `sendkey` commands sent over the QEMU monitor socket. Used by the `smoke*` Makefile targets to automate typed input.

```
Usage: python3 qemu_sendkeys.py [--delay MS] < commands.txt
       --delay: milliseconds between keystrokes (default 40)
```

Supports the full US layout including shift for uppercase letters and symbols. Special keys: `spc` (space), `ret` (Enter), `.`, `-`, `/`.

### `scripts/tcp_smoke_server.py`

A simple host-side TCP server used during `smoke*-net` tests to verify that MrHakOS's TCP stack successfully delivers a payload.

```
Usage: python3 tcp_smoke_server.py [PORT] [OUTPUT_FILE]
       Default: port 8080, output scripts/bin/tcp-received.log
       Timeout: 45 seconds overall, 5 seconds per read
```

Accepts one connection, captures all received bytes, and writes them to the output file. Exits after the connection closes.

### `scripts/udp_server.py`

A minimal UDP listener for manual testing of the `udp` terminal command.

```
Usage: python3 scripts/udp_server.py
       Listens on 0.0.0.0:9001 indefinitely
```

Prints each received datagram with its source address.

### `scripts/build_iso_usb.sh`

An interactive helper for writing `bin/mrhakos-grub.iso` to a USB drive. Includes safety checks:

- Confirms the target is a whole disk device (e.g., `/dev/sdb`), not a partition (`/dev/sdb1`)
- Checks the device is not mounted
- Shows device info and asks for explicit confirmation
- Supports `--yes` to skip the prompt and `--dry-run` to skip the actual `dd`

```bash
./scripts/build_iso_usb.sh --device /dev/sdX          # with confirmation prompt
./scripts/build_iso_usb.sh --device /dev/sdX --yes    # no prompt
./scripts/build_iso_usb.sh --device /dev/sdX --dry-run
```

---

## Technical Deep Dive

### 64-bit Stack Overflow

In 64-bit mode, the kernel stack is only 4 KB. Large local arrays overflow it silently, causing a crash when the corrupted stack is used. The fix is to mark buffers as `static` — they then live in BSS/data instead of on the stack.

```cpp
// WRONG — 512+ bytes on the stack → crash in 64-bit
void processCommand(const char* cmd) {
    char inputBuf[256];
    char argsBuf[256];
    // ...
}

// RIGHT — static storage, no stack pressure
void processCommand(const char* cmd) {
    static char inputBuf[256];
    static char argsBuf[256];
    // ...
}
```

This is especially important inside `terminal.cpp` and `network.cpp` where many intermediate buffers are needed.

### ISR Stack Alignment (x86-64)

The x86-64 System V ABI requires the stack pointer to be 16-byte aligned before every `call` instruction. Interrupt handlers are called by the CPU, not by software, so the alignment must be enforced manually.

When an interrupt fires in 64-bit mode:
- CPU pushes: SS, RSP, RFLAGS, CS, RIP → **5 × 8 = 40 bytes**
- `isr64.asm` pushes: RAX, RCX, RDX, RBX, RBP, RSI, RDI, R8–R15 → **15 × 8 = 120 bytes**
- Total on entry to C handler: **160 bytes = 16-byte aligned** ✓

Without this, calling any C++ function from the ISR could corrupt SSE/AVX register state or cause a misalignment fault.

### Bootloader Size Constraint

The entire bootloader must fit within 510 bytes (512 bytes minus the 2-byte MBR signature `0x55AA`). NASM assembles the bootloader, and QEMU verifies the last two bytes. The 64-bit bootloader (`bootloader64.asm`) is more space-constrained because it also builds page tables and loads a 64-bit GDT, which is why it uses fewer diagnostic `print` calls.

### Freestanding C++

MrHakOS compiles with `-ffreestanding -nostdlib`. This means:
- No `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<new>`, or any standard library header is available
- `operator new` / `operator delete` must be implemented manually (bump allocator in `filesystem.cpp`)
- `memset` / `memcpy` are provided by `string.cpp` (the linker would otherwise emit an undefined symbol for the compiler-generated intrinsic calls)
- Exceptions and RTTI are disabled (`-fno-exceptions -fno-rtti`) — they require runtime support that does not exist

---

## Debugging Tips

### Serial Output

Run QEMU with serial connected to your terminal:

```bash
qemu-system-x86_64 -serial stdio -drive format=raw,file=bin/mrhakos64.img
```

Serial output from `Serial::print()` calls in the kernel will appear in your terminal. The network stack logs each packet send/receive step.

### QEMU Monitor

Press `Ctrl+Alt+2` inside QEMU to open the monitor:

```
info registers         # dump all CPU registers
info mem               # dump page table / memory mappings
x /10i $rip            # disassemble 10 instructions at current RIP
x /10xg 0xb8000        # inspect VGA text buffer
```

Press `Ctrl+Alt+1` to return to the VGA display.

### Common Issues

| Symptom | Likely Cause | Fix |
|---|---|---|
| Black screen after boot | GDT descriptor malformed or 64-bit jump target wrong | Check `dq` vs `dd` in GDT; verify entry point offset |
| Keyboard types nothing | IRQ1 not firing; USB legacy not enabled | Run `kbd` to check counters; enable USB Legacy in BIOS |
| Crash on first command | Stack overflow from large local arrays | Change `char buf[N]` to `static char buf[N]` in the crashing function |
| No network detected | PCI scan returned no RTL NIC | Ensure `make run64-net` is used (not `make run64`); check `netinfo` |
| `ping` works locally but not public IPs | QEMU SLIRP blocks raw ICMP | Use `http 1.1.1.1` to test public connectivity instead |
| GRUB shows blank screen after selecting MrHakOS | Wrong GRUB command (`linux` instead of `multiboot2`) | Use `multiboot2 /mrhakos/kernel.elf` in the GRUB entry |

---

## Future Roadmap

- [ ] Physical and virtual memory management (page frame allocator)
- [ ] User mode and ring-3 isolation
- [ ] System calls (syscall/sysret)
- [ ] Process scheduling and multitasking
- [ ] Disk I/O driver (IDE/AHCI) and persistent filesystem
- [ ] TCP retransmission, receive buffering, listen/accept sockets
- [ ] TLS/HTTPS support
- [ ] SOCKS5 transport through an external Tor proxy
- [ ] Native onion/Tor transport for anonymous chat
- [ ] Secure P2P text chat over direct TCP
- [ ] UEFI bootloader (replacing the BIOS custom bootloader)
- [ ] Graphics mode (VESA / GOP framebuffer)
- [x] RTL8139 Ethernet + ARP + IPv4 + DHCP
- [x] ICMP ping and traceroute
- [x] UDP text send
- [x] DNS A-record lookup
- [x] TCP minimal client (SYN/ACK/PSH/FIN)
- [x] HTTP GET response receive
- [x] RTL8111/RTL8168/RTL8169 real hardware driver
- [x] USB legacy keyboard fallback polling
- [x] GRUB Multiboot2 boot path
- [x] `cp` and `mv` filesystem commands

---

## Learning Resources

- [OSDev Wiki](https://wiki.osdev.org/) — the single best resource for OS development on x86
- [Intel 64 and IA-32 Architectures Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) — authoritative reference for all CPU behavior
- [AMD64 Architecture Programmer's Manual](https://www.amd.com/en/support/tech-docs) — useful complement for 64-bit specifics
- [Realtek RTL8139 Programming Guide](https://web.archive.org/web/20200416023534/http://www.cs.usfca.edu/~cruse/cs326f04/RTL8139_ProgrammersGuide.pdf) — datasheet for the QEMU NIC driver
- [Writing a Simple Operating System from Scratch (Nick Blundell)](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf) — good starting point for bootloader + protected mode

---

## Author

**Mohamed Hakkou (ImMrHak)**

---

*Built with Assembly, C++, and an unreasonable amount of debugging.*

> If you can't explain your code three months later — that's not a bug, that's a feature.
