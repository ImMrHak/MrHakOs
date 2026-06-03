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
- `netinfo` - Show PCI/Ethernet detection state, MAC/IP address, link, and RX/TX/protocol counters
- `arping` - Resolve an IPv4 peer with ARP
- `ping` - Linux-like ICMP echo with repeated replies, TTL/time, statistics, and hostname support
- `dhcp` - Automatically configure IP/gateway/DNS/netmask from DHCP
- `traceroute` - Show ICMP route hops with TTL-limited echo probes
- `udp` - Send a small UDP text packet to a host listener
- `tcp` - Open a minimal TCP connection, send text, and close it
- `http` - Fetch `/` from a public HTTP server over TCP port 80

### Technical Features
- **Interrupt Handling**: Custom IDT setup with keyboard ISR
- **Serial Logging**: COM1 debug output for boot/network diagnostics
- **Networking Foundation**: PCI scanner, QEMU RTL8139 plus real-hardware Realtek RTL8111/RTL8168/RTL8169 init, Ethernet TX/RX polling, ARP, static IPv4 plus DHCP, Linux-like ICMP ping, ICMP traceroute, UDP text send, DNS A-record lookup, minimal TCP client send/close, basic HTTP GET response receive, MAC/link reporting, and `netinfo` command
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
├── README.md             # This file
├── scripts/
│   └── qemu_sendkeys.py  # Helper for interactive QEMU smoke tests
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
| **i686-elf-gcc/g++ or i686-linux-gnu-gcc/g++** | 32-bit freestanding build | OSDev cross-toolchain or Kali/Debian packages |
| **clang++/lld/llvm-objcopy** | 64-bit compiler/tooling | Package manager |
| **QEMU** | Emulator | [qemu.org](https://www.qemu.org/) |

### Installation

#### Kali/Debian/Ubuntu
```bash
sudo apt-get update
sudo apt-get install -y nasm qemu-system-x86 clang lld llvm \
    gcc-i686-linux-gnu g++-i686-linux-gnu binutils-i686-linux-gnu
```

The Makefile prefers `i686-elf-*` when available, then falls back to Kali/Debian's
`i686-linux-gnu-*` tools for the 32-bit freestanding build. Run `make doctor` or
`make install-deps-help` to check tools and print the exact install command.

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
make clean && make all32 && make run32
```

### Build and Run (64-bit)
```bash
make clean && make all64 && make run64
```

### Build and Run with QEMU RTL8139 Networking
```bash
make run32-net
make run64-net
```

### Boot from Kali Linux GRUB on real hardware

For the Kali GRUB menu, boot the 32-bit ELF kernel through GRUB's Multiboot2
loader. Do **not** use GRUB's `linux` command for MrHakOS, and do **not** try to
chainload `bin/mrhakos.img`; that raw floppy image is for the custom BIOS
bootloader/QEMU path.

Build the GRUB-ready assets:
```bash
make clean && make all32
make grub-assets
```

Install them into Kali's `/boot` and add a separate GRUB script:
```bash
sudo mkdir -p /boot/mrhakos
sudo cp bin/kernel.elf /boot/mrhakos/kernel.elf
sudo cp bin/41_mrhakos /etc/grub.d/41_mrhakos
sudo chmod +x /etc/grub.d/41_mrhakos
sudo update-grub
```

The generated `/etc/grub.d/41_mrhakos` menuentry is:
```grub
menuentry "MrHakOS 32-bit (Multiboot2)" {
    insmod multiboot2
    multiboot2 /mrhakos/kernel.elf
    boot
}
```

After rebooting, select `MrHakOS 32-bit (Multiboot2)` from the Kali GRUB menu.
You should see the MrHakOS terminal prompt, not a blank GRUB screen.

To test a bootable ISO/USB path instead:
```bash
make grubiso
# then write bin/mrhakos-grub.iso to a USB with your preferred imaging tool
```

These targets attach QEMU's emulated RTL8139 Ethernet card and route COM1 serial logs to the terminal. `make run64-net` is the right 64-bit command for the current QEMU networking path. If you use plain `make run64`, no NIC is attached and the OS cannot detect a network card.

Inside MrHakOS, run:
```bash
netinfo
dhcp
netinfo
ping 1.1.1.1
```

On Mohamed's current laptop/Kali host, the wired Ethernet card is:

```text
Realtek RTL8111/8168/8211/8411 PCI Express Gigabit Ethernet Controller [10ec:8168]
```

MrHakOS now has a first native RTL8111/RTL8168/RTL8169 descriptor-ring driver in
addition to the older QEMU RTL8139 path. The real-hardware path is still polled
instead of interrupt/MSI-driven, but it should detect the cable link, allow DHCP,
then allow commands such as `ping 1.1.1.1` when the router replies.

If `netinfo` says `Unsupported Ethernet device`, the machine is using a different
NIC family and MrHakOS needs a separate driver for that PCI vendor/device ID.

Expected networking milestone output in QEMU:
```text
NIC: RTL8139
MAC: 52:54:00:12:34:56
IP: 10.0.2.15
Gateway: 10.0.2.2
DNS: 10.0.2.3
RX: 1 packets
TX: 3 packets
ARP: 1 packets
ICMP: 1 packets
```

Try the first working packet commands:
```bash
arping 10.0.2.2
dhcp
ping 10.0.2.2
traceroute 10.0.2.2
udp 10.0.2.2 hello-from-mrhakos
dns example.com
tcp 10.0.2.2 8080 hello-from-mrhakos
http 1.1.1.1
http example.com
```

For `udp`, run a UDP listener on the host before starting QEMU:
```bash
python3 - <<'PY'
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('0.0.0.0', 9001))
print('listening on UDP 9001')
print(s.recvfrom(2048))
PY
```

MrHakOS sends to the QEMU user-network host alias `10.0.2.2:9001`.

For `tcp`, run a TCP listener on the host before starting QEMU:
```bash
python3 - <<'PY'
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', 8080))
s.listen(1)
print('listening on TCP 8080')
c, a = s.accept()
print('accepted', a)
print(c.recv(2048))
c.close()
s.close()
PY
```

Then inside MrHakOS:
```bash
tcp 10.0.2.2 8080 hello-from-mrhakos
```

Current TCP scope is intentionally small: client-side SYN, SYN-ACK handling,
ACK, one PSH/ACK payload, and FIN close. Retransmission, receive buffering,
server/listen sockets, congestion control, HTTP, TLS, SOCKS5, and Tor come in
later layers.

To communicate with a public server without running anything locally, use the
new HTTP command inside MrHakOS:
```bash
http 1.1.1.1
http example.com
```

Verified example output from `http 1.1.1.1` includes a real Cloudflare response:
```text
HTTP/1.1 301 Moved Permanently
Server: cloudflare
Location: https://1.1.1.1/
```

Note: `ping 10.0.2.2` works through QEMU user networking. Public raw ICMP such
as `ping 1.1.1.1` may be blocked or not translated by QEMU SLIRP/user-mode NAT
even though TCP/HTTP to public IPs works. For public reachability testing in
QEMU user networking, prefer `http 1.1.1.1` or `tcp PUBLIC_IP 80 hello` until
MrHakOS is running on a TAP/bridged network or real hardware NIC path.

For actual internet routing through QEMU NAT, send to a public UDP listener by
IP address or hostname. QEMU user networking provides:

- Default/static MrHakOS IP: `10.0.2.15`
- Gateway: `10.0.2.2`
- DNS: `10.0.2.3`
- `dhcp` can request the same values automatically from QEMU user networking

Examples inside MrHakOS:
```bash
dns example.com
udp YOUR_PUBLIC_SERVER_IP hello-from-mrhakos-internet
udp your-server.example.com hello-from-mrhakos-internet
```

Hostnames are resolved with a minimal UDP DNS A-record query. Full web browsing
still needs the next milestones: TCP receive/retransmission, then HTTP/TLS.

Tor/anonymity roadmap: MrHakOS still should not try native Tor before the TCP
layer is stronger and crypto/runtime support exists. The safer next bridge is a
SOCKS5 TCP transport so MrHakOS can route chat traffic through an external Tor
proxy first, then later replace that with a native Tor/onion client. Tor mode
must be a separate transport backend so direct-IP mode cannot leak traffic when
an anonymous chat session is requested.

### USB/laptop keyboard notes for real hardware

MrHakOS now has a real-hardware keyboard fallback in addition to IRQ1 keyboard
input: the terminal loop polls the i8042-compatible data port. This helps USB
keyboards and many laptop keyboards when firmware exposes them through "USB
Legacy Support" but the normal PS/2 keyboard interrupt is not delivered after a
GRUB boot.

After booting on real hardware, type:
```bash
kbd
```

It prints keyboard counters:
- `IRQ1 scancodes` means the classic keyboard interrupt path is working.
- `Polled scancodes` means the fallback path is receiving translated USB/laptop
  keystrokes without relying on IRQ1.

If the keyboard still does not type anything, enter your BIOS/UEFI setup and
enable options named like `USB Legacy Support`, `Legacy USB Keyboard`, `USB
Keyboard Support`, or `CSM/Legacy Boot`. A full native xHCI/EHCI/OHCI USB HID
stack is a later kernel driver milestone; this fallback is the smallest safe fix
for trying MrHakOS on real laptops now.

### Smoke Tests
```bash
make smoke
```

`make smoke` boots both images headlessly in QEMU, types a small command script
(`help`, `mkdir docs`, `touch hello.hak`, `echo hello > hello.hak`, `cat`, `ls`),
and writes screenshots to `bin/smoke32.ppm` and `bin/smoke64.ppm`.

Network smoke tests are also available:
```bash
make smoke32-net smoke64-net
```

They attach the RTL8139 device, type `netinfo`, `dns example.com`,
`arping 10.0.2.2`, `ping 10.0.2.2`, `udp 10.0.2.2 hello-from-mrhakos`,
`tcp 10.0.2.2 8080 hello-tcp-from-mrhakos`, and `netinfo` again.
Screenshots are written to `bin/smoke32-net.ppm` / `bin/smoke64-net.ppm`, and
COM1 serial logs are captured in `bin/serial32-net.log` / `bin/serial64-net.log`.
The smoke target also starts a tiny host TCP listener and verifies the received
payload in `bin/tcp32-received.log` / `bin/tcp64-received.log`.

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

### Debug Markers
Most visible VGA debug markers have been removed or turned into blank VGA touches
while the 64-bit boot path is being stabilized. Future debugging should prefer
serial logging over screen markers.

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
- [x] Early network stack: RTL8139, Ethernet, ARP, IPv4, DHCP, Linux-like ICMP ping, traceroute, UDP, DNS, minimal TCP client, HTTP GET
- [ ] TCP retransmission/receive buffering/listen sockets
- [ ] Secure P2P chat over direct TCP
- [ ] SOCKS5 transport through external Tor proxy
- [ ] Native Tor/onion transport
- [ ] Graphics mode support
- [ ] UEFI bootloader option

## 📜 License

This project is open source and available for educational purposes.

## 👨‍💻 Author

**Mohamed Hakkou (ImMrHak)**

---

*Built with ❤️, Assembly, C++, and a lot of debugging*

> **Remember**: If you can't explain your code 3 months later, that's a feature, not a bug! 😄