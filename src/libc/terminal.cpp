#include <terminal.hpp>
#include <string.hpp>
#include <interrupts.hpp>
#include <serial.hpp>

static const char* TERMINAL_HEX = "0123456789ABCDEF";

static void u32ToDec(uint32_t value, char* out, int outLen) {
    if (!out || outLen <= 0) {
        return;
    }
    if (value == 0) {
        if (outLen > 1) {
            out[0] = '0';
            out[1] = '\0';
        } else {
            out[0] = '\0';
        }
        return;
    }

    char tmp[11];
    int i = 0;
    while (value > 0 && i < 10) {
        tmp[i++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    int p = 0;
    while (i > 0 && p < outLen - 1) {
        out[p++] = tmp[--i];
    }
    out[p] = '\0';
}

static void u32ToHex(uint32_t value, char* out, int outLen) {
    if (!out || outLen < 11) {
        return;
    }
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 8; i++) {
        out[2 + i] = TERMINAL_HEX[(value >> ((7 - i) * 4)) & 0xF];
    }
    out[10] = '\0';
}


// Global terminal instance for keyboard handler
Terminal* g_terminal = nullptr;

// Keyboard handler function that's registered with the interrupt system
void terminal_keyboard_handler() {
    if (g_terminal) {
        Serial::writeString("[terminal] keyboard callback\n");
        g_terminal->handleKeypress();
    } else {
        Serial::writeString("[terminal] keyboard callback missing terminal\n");
    }
}

Terminal::Terminal() {
    inputPosition = 0;
    readingInput = false;
    commandReady = false;
    network = nullptr;
    lastDhcpState = 0;

    // Initialize input buffer without relying on hosted-library memset.
    volatile char* buf = inputBuffer;
    for (int i = 0; i < 256; i++) {
        buf[i] = 0;
    }

    g_terminal = this;
}

void Terminal::init(Vga* vga, Interrupts* interrupts, FileSystem* filesystem, Network* network) {
    this->vga = vga;
    this->interrupts = interrupts;
    this->filesystem = filesystem;
    this->network = network;
    interrupts->registerHandler(1, terminal_keyboard_handler);
    vga->clear();
}

void Terminal::run() {
    readingInput = true;
    putString("Welcome MrHakOs Terminal\n");
    putString("Network: type dhcp when the cable/router link is ready.\n");
    showPrompt();
    
    // Wait for keyboard interrupts. Also poll the i8042 data port on every
    // timer wake so USB keyboards exposed through firmware USB Legacy Support
    // still work on real hardware when IRQ1 is not delivered after GRUB.
    while (true) {
        pollKeyboard();

        if (network) {
            network->tickDhcp();
            uint8_t dhcpState = network->getDhcpState();
            if (dhcpState != lastDhcpState) {
                lastDhcpState = dhcpState;
                if (dhcpState == 3) {
                    const NetworkInfo& info = network->getInfo();
                    static char ip[16];
                    putString("\nDHCP configured\n");
                    putString("  IP: "); network->formatIp(info.ipAddress, ip, sizeof(ip)); putString(ip); putString("\n");
                    putString("  Gateway: "); network->formatIp(info.gatewayIp, ip, sizeof(ip)); putString(ip); putString("\n");
                    putString("  DNS: "); network->formatIp(info.dnsIp, ip, sizeof(ip)); putString(ip); putString("\n");
                    putString("  Netmask: "); network->formatIp(info.netmask, ip, sizeof(ip)); putString(ip); putString("\n");
                    showPrompt();
                } else if (dhcpState == 4) {
                    putString("\nDHCP failed or timed out; network remains unconfigured\n");
                    showPrompt();
                }
            }
        }

        // Process command outside interrupt context to avoid long ISR work
        if (commandReady) {
            Serial::writeString("[terminal] commandReady in loop\n");
            commandReady = false;
            inputBuffer[inputPosition] = '\0';
            processCommand(inputBuffer);
            inputPosition = 0;
            inputBuffer[0] = '\0';
            showPrompt();
        }
        // Do not rely solely on HLT here. Some real GRUB/USB-legacy boots do
        // not deliver IRQ1, and a few machines also do not route the PIT/PIC as
        // expected. A tiny pause loop keeps polling responsive for USB/laptop
        // keyboards while avoiding a completely tight spin.
        for (volatile int spin = 0; spin < 10000; ++spin) {
            asm volatile("pause");
        }
    }
}

void Terminal::putChar(char c) {
    vga->putChar(c);
}

void Terminal::putString(const char* str) {
    vga->set_cursor_enabled(false);
    for (int i = 0; str[i]; i++) {
        putChar(str[i]);
    }
    vga->set_cursor_enabled(true);
    vga->force_update_cursor();
}

void Terminal::handleKeypress() {
    char key = getLastKey();
    
    if (key == 0) {
        Serial::writeString("[terminal] key zero\n");
        return;
    }
    Serial::writeString(readingInput ? "[terminal] reading key=0x" : "[terminal] not reading key=0x");
    Serial::writeHex8(static_cast<uint8_t>(key));
    Serial::writeString(" pos=0x");
    Serial::writeHex32(static_cast<uint32_t>(inputPosition));
    Serial::writeString("\n");
    
    if (readingInput) {
        if (inputPosition < 0 || inputPosition >= 255) {
            Serial::writeString("[terminal] input position repaired\n");
            inputPosition = 0;
            inputBuffer[0] = '\0';
        }
        if (key == '\n' || key == '\r') {
            commandReady = true;
        } else if (key == '\b') {
            if (inputPosition > 0) {
                inputPosition--;
                inputBuffer[inputPosition] = 0;
                int cx = vga->get_x();
                int cy = vga->get_y();
                if (cx > 0) {
                    cx--;
                } else if (cy > 0) {
                    cy--;
                    cx = 79; // move to last column of previous line
                } else {
                    // Top-left corner: nothing to erase
                }
                // Erase character visually and move cursor
                vga->putCharAt(' ', cx, cy);
                vga->set_xy(cx, cy);
            }
        } else if (inputPosition < 255) { // Regular character
            inputBuffer[inputPosition] = key;
            inputPosition++;
            
            // Echo the character
            putChar(key);
        }
    }
}

void Terminal::processCommand(const char* cmd) {
    // Use static buffers instead of stack arrays to avoid 64-bit stack issues
    // Static variables are zero-initialized by default in .bss section
    static char command[256];
    static char args[256];
    
    // Clear buffers for reuse
    command[0] = '\0';
    args[0] = '\0';
    
    // Find the first space to separate command from arguments
    int i = 0;
    while (cmd[i] != ' ' && cmd[i] != '\0') {
        command[i] = cmd[i];
        i++;
    }
    command[i] = '\0';
    
    
    // Extract arguments if any
    if (cmd[i] == ' ') {
        int j = 0;
        i++; // Skip the space
        while (cmd[i] != '\0') {
            args[j] = cmd[i];
            i++;
            j++;
        }
        args[j] = '\0';
    }

    // Process commands
    if (strcmp(command, "clear") == 0) {
        vga->clear();
        putString("Welcome MrHakOs Terminal\n");
    } else if (strcmp(command, "help") == 0) {
        putString("\n");
        putString("Available commands:\n");
        putString("  clear   - Clear the screen\n");
        putString("  help    - Show this help message\n");
        putString("  mrhakos - Show MrHakOs information\n");
        putString("  kbd     - Show keyboard IRQ/poll counters for USB legacy debugging\n");
        putString("  mkdir   - Create a new directory\n");
        putString("  ls      - List files and directories\n");
        putString("  cd      - Change current directory\n");
        putString("  touch   - Create a new .hak file\n");
        putString("  cat     - Display the content of a .hak file\n");
        putString("  echo    - Display text or write to a .hak file using > redirection\n");
        putString("  cp      - Copy a file\n");
        putString("  mv      - Move a file or directory\n");
        putString("  netinfo - Show detected Ethernet/PCI network state\n");
        putString("  netpoll - Poll RTL8139 receive queue once\n");
        putString("  arping  - Resolve an IPv4 address with ARP, example: arping 10.0.2.2\n");
        putString("  ping    - Linux-like ICMP echo, example: ping example.com\n");
        putString("  dhcp    - Auto-configure IP/gateway/DNS with DHCP\n");
        putString("  traceroute - Show ICMP route hops, example: traceroute example.com\n");
        putString("  udp     - Send UDP text to port 9001, example: udp 10.0.2.2 hello\n");
        putString("            Hostnames work too after DNS, example: udp myserver.com hello\n");
        putString("  dns     - Resolve a hostname with QEMU DNS, example: dns example.com\n");
        putString("  tcp     - Send TCP text, example: tcp 10.0.2.2 8080 hello\n");
        putString("  http    - Fetch / over TCP port 80, example: http example.com\n");
        putString("  securechat - Onion-only secure chat control, example: securechat status\n");
    } else if (strcmp(command, "mrhakos") == 0) {
        putString("\n");
        putString("Name: MrHakOS\n");
        putString("Author : Mohamed Hakkou\n");
        putString("Version : 5.0\n");
        putString("Description : A simple operating system using C++\n");
    } else if (strcmp(command, "kbd") == 0) {
        char value[16];
        putString("\nKeyboard input counters:\n");
        u32ToDec(keyboardIrqScancodes(), value, sizeof(value));
        putString("  IRQ1 scancodes: ");
        putString(value);
        putString("\n");
        u32ToDec(keyboardPolledScancodes(), value, sizeof(value));
        putString("  Polled scancodes: ");
        putString(value);
        putString("\n");
        putString("If a USB keyboard works here, firmware USB Legacy Support is translating it.\n");
    } else if (strcmp(command, "mkdir") == 0) {
        cmdMkdir(args);
    } else if (strcmp(command, "ls") == 0) {
        cmdLs(args);
    } else if (strcmp(command, "cd") == 0) {
        cmdCd(args);
    } else if (strcmp(command, "cp") == 0) {
        cmdCp(args);
    } else if (strcmp(command, "mv") == 0) {
        cmdMv(args);
    } else if (strcmp(command, "touch") == 0) {
        cmdTouch(args);
    } else if (strcmp(command, "cat") == 0) {
        cmdCat(args);
    } else if (strcmp(command, "echo") == 0) {
        cmdEcho(args);
    } else if (strcmp(command, "netinfo") == 0) {
        cmdNetinfo(args);
    } else if (strcmp(command, "netpoll") == 0) {
        cmdNetpoll(args);
    } else if (strcmp(command, "arping") == 0) {
        cmdArping(args);
    } else if (strcmp(command, "ping") == 0) {
        cmdPing(args);
    } else if (strcmp(command, "dhcp") == 0) {
        cmdDhcp(args);
    } else if (strcmp(command, "traceroute") == 0 || strcmp(command, "trace") == 0) {
        cmdTraceroute(args);
    } else if (strcmp(command, "udp") == 0) {
        cmdUdp(args);
    } else if (strcmp(command, "dns") == 0) {
        cmdDns(args);
    } else if (strcmp(command, "tcp") == 0) {
        cmdTcp(args);
    } else if (strcmp(command, "http") == 0) {
        cmdHttp(args);
    } else if (strcmp(command, "securechat") == 0) {
        cmdSecureChat(args);
    } else if (strcmp(command, "") == 0){
        putString("\n");
    } else if (command[0] != '\0') {
        putString("\nUnknown command: ");
        putString(command);
        putString("\n");
    }
}


void Terminal::showPrompt() {
    // Show prompt
    const char* prompt = "MrHakOS >> ";
    putString(prompt);
}

void Terminal::onKeypress() {
    handleKeypress();
}

// Command handler methods
void Terminal::cmdMkdir(const char* args) {
    if (args[0] == '\0') {
        putString("\nError: mkdir requires a directory name\n");
        return;
    }
    
    if (filesystem->mkdir(args)) {
        putString("\nDirectory created: ");
        putString(args);
        putString("\n");
    } else {
        putString("\nError: Could not create directory\n");
    }
}

void Terminal::cmdLs(const char* args) {
    // Unused parameter
    (void)args;
    putString("\n");
    
    // Get current directory
    FileSystemEntry* currentDir = filesystem->getCurrentDirectory();
    
    // Display current path
    char path[256];
    filesystem->getCurrentPath(path, 256);
    putString("Contents of ");
    putString(path);
    putString(":\n");
    
    // Check if directory is empty
    if (currentDir->childCount == 0) {
        putString("  <empty>\n");
        return;
    }
    
    // List all entries in current directory
    for (size_t i = 0; i < currentDir->childCount; i++) {
        FileSystemEntry* entry = currentDir->children[i];
        
        putString("  ");
        
        // Print entry name
        putString(entry->name);
        
        // Add trailing slash for directories
        if (entry->type == FS_TYPE_DIRECTORY) {
            putChar('/');
        }
        
        putString("\n");
    }
}

void Terminal::cmdCd(const char* args) {
    if (args[0] == '\0') {
        // No arguments, show current path
        char path[256];
        filesystem->getCurrentPath(path, 256);
        putString("\nCurrent directory: ");
        putString(path);
        putString("\n");
        return;
    }
    
    if (filesystem->cd(args)) {
        // Success, show new path
        char path[256];
        filesystem->getCurrentPath(path, 256);
        putString("\nChanged to: ");
        putString(path);
        putString("\n");
    } else {
        putString("\nError: Could not change directory\n");
    }
}

void Terminal::cmdCp(const char* args) {
    // Use static buffers instead of stack arrays to avoid 64-bit stack overflow
    static char source[256];
    static char destination[256];
    
    // Clear buffers
    source[0] = '\0';
    destination[0] = '\0';
    
    // Find the first space to separate source from destination
    int i = 0;
    while (args[i] != ' ' && args[i] != '\0') {
        source[i] = args[i];
        i++;
    }
    source[i] = '\0';
    
    // Extract destination if any
    if (args[i] == ' ') {
        int j = 0;
        i++; // Skip the space
        while (args[i] != '\0') {
            destination[j] = args[i];
            i++;
            j++;
        }
        destination[j] = '\0';
    }
    
    if (source[0] == '\0' || destination[0] == '\0') {
        putString("\nError: cp requires source and destination\n");
        return;
    }
    
    if (filesystem->cp(source, destination)) {
        putString("\nFile copied: ");
        putString(source);
        putString(" -> ");
        putString(destination);
        putString("\n");
    } else {
        putString("\nError: Could not copy file. Make sure source exists and destination doesn't, and both are .hak files.\n");
    }
}

void Terminal::cmdMv(const char* args) {
    // Use static buffers instead of stack arrays to avoid 64-bit stack overflow
    static char source[256];
    static char destination[256];
    
    // Clear buffers
    source[0] = '\0';
    destination[0] = '\0';
    
    // Find the first space to separate source from destination
    int i = 0;
    while (args[i] != ' ' && args[i] != '\0') {
        source[i] = args[i];
        i++;
    }
    source[i] = '\0';
    
    // Extract destination if any
    if (args[i] == ' ') {
        int j = 0;
        i++; // Skip the space
        while (args[i] != '\0') {
            destination[j] = args[i];
            i++;
            j++;
        }
        destination[j] = '\0';
    }
    
    if (source[0] == '\0' || destination[0] == '\0') {
        putString("\nError: mv requires source and destination\n");
        return;
    }
    
    if (filesystem->mv(source, destination)) {
        putString("\nMoved/renamed: ");
        putString(source);
        putString(" -> ");
        putString(destination);
        putString("\n");
    } else {
        putString("\nError: Could not move/rename. Make sure source exists, destination doesn't, and .hak files keep their extension.\n");
    }
}

void Terminal::cmdTouch(const char* args) {
    if (args[0] == '\0') {
        putString("\nError: touch requires a filename\n");
        return;
    }
    
    // Check if the filename has .hak extension
    const char* extension = ".hak";
    size_t argsLen = strlen(args);
    size_t extLen = strlen(extension);
    
    if (argsLen <= extLen || strcmp(args + argsLen - extLen, extension) != 0) {
        putString("\nError: Only .hak files are supported\n");
        return;
    }
    
    // Create an empty .hak file
    if (filesystem->createHakFile(args, "")) {
        putString("\nFile created: ");
        putString(args);
        putString("\n");
    } else {
        putString("\nError: Could not create file\n");
    }
}

void Terminal::cmdCat(const char* args) {
    if (args[0] == '\0') {
        putString("\nError: cat requires a filename\n");
        return;
    }
    
    // Buffer to store file content
    char buffer[FS_MAX_FILE_SIZE];
    
    // Read file content
    if (filesystem->readHakFile(args, buffer, FS_MAX_FILE_SIZE)) {
        putString("\nContent of ");
        putString(args);
        putString(":\n");
        
        // Display file content
        if (buffer[0] == '\0') {
            putString("  <empty file>\n");
        } else {
            putString(buffer);
            putString("\n");
        }
    } else {
        putString("\nError: Could not read file\n");
    }
}

void Terminal::cmdEcho(const char* args) {
    if (args[0] == '\0') {
        putString("\n");
        return;
    }
    
    // Use static buffers instead of stack arrays to avoid 64-bit stack overflow
    static char text[256];
    static char filename[256];
    
    // Clear buffers
    text[0] = '\0';
    filename[0] = '\0';
    bool redirect = false;
    
    // Find the '>' character for redirection
    int i = 0;
    int textEnd = 0;
    while (args[i] != '\0') {
        if (args[i] == '>') {
            redirect = true;
            textEnd = i;
            break;
        }
        i++;
    }
    
    if (redirect) {
        // Extract the text to echo (excluding trailing spaces)
        for (i = 0; i < textEnd; i++) {
            text[i] = args[i];
        }
        text[textEnd] = '\0';
        
        // Trim trailing spaces
        while (textEnd > 0 && text[textEnd - 1] == ' ') {
            text[--textEnd] = '\0';
        }
        
        // Extract the filename (skipping '>' and leading spaces)
        i = textEnd + 1;
        while (args[i] == ' ' || args[i] == '>') {
            i++;
        }
        
        int j = 0;
        while (args[i] != '\0') {
            filename[j++] = args[i++];
        }
        filename[j] = '\0';
        
        // Check if filename is empty
        if (filename[0] == '\0') {
            putString("\nError: No filename specified for redirection\n");
            return;
        }
        
        // Check if the filename has .hak extension
        if (!filesystem->hasExtension(filename, ".hak")) {
            putString("\nError: Only .hak files are supported\n");
            return;
        }
        
        // Write to file
        if (filesystem->createHakFile(filename, text)) {
            putString("\nContent written to ");
            putString(filename);
            putString("\n");
        } else {
            putString("\nError: Could not write to file\n");
        }
    } else {
        // Just echo the text
        putString("\n");
        putString(args);
        putString("\n");
    }
}

void Terminal::cmdNetinfo(const char* args) {
    (void)args;
    putString("\nNetwork information:\n");

    if (!network) {
        putString("  Network subsystem: unavailable\n");
        return;
    }

    const NetworkInfo& info = network->getInfo();
    char number[16];
    char hex[11];
    char mac[18];

    putString("  NIC: ");
    if (info.rtl8139Present) {
        putString(info.nicName);
    } else if (info.pciPresent) {
        putString("Unsupported Ethernet device");
    } else {
        putString("not detected");
    }
    putString("\n");

    putString("  MAC: ");
    if (info.rtl8139Present) {
        network->formatMac(mac, sizeof(mac));
        putString(mac);
    } else {
        putString("--:--:--:--:--:--");
    }
    putString("\n");

    putString("  IP: ");
    if (info.ipAddress) { network->formatIp(info.ipAddress, number, sizeof(number)); putString(number); }
    else { putString("not configured"); }
    putString("\n");

    putString("  Gateway: ");
    if (info.gatewayIp) { network->formatIp(info.gatewayIp, number, sizeof(number)); putString(number); }
    else { putString("not configured"); }
    putString("\n");

    putString("  DNS: ");
    if (info.dnsIp) { network->formatIp(info.dnsIp, number, sizeof(number)); putString(number); }
    else { putString("not configured"); }
    putString("\n");

    putString("  Netmask: ");
    if (info.netmask) { network->formatIp(info.netmask, number, sizeof(number)); putString(number); }
    else { putString("not configured"); }
    putString("\n");

    putString("  Config: ");
    putString(info.dhcpConfigured ? "DHCP" : "static/default");
    putString("\n");

    putString("  Link: ");
    putString(info.linkUp ? "up" : "unknown/down");
    putString("\n");

    putString("  Register base: ");
    u32ToHex(info.ioBase, hex, sizeof(hex));
    putString(hex);
    putString("\n");

    putString("  IRQ: ");
    u32ToDec(info.irqLine, number, sizeof(number));
    putString(number);
    putString("\n");

    putString("  RX: ");
    u32ToDec(info.rxPackets, number, sizeof(number));
    putString(number);
    putString(" packets\n");

    putString("  TX: ");
    u32ToDec(info.txPackets, number, sizeof(number));
    putString(number);
    putString(" packets\n");

    putString("  ARP: ");
    u32ToDec(info.arpPackets, number, sizeof(number));
    putString(number);
    putString(" packets\n");

    putString("  IPv4: ");
    u32ToDec(info.ipv4Packets, number, sizeof(number));
    putString(number);
    putString(" packets\n");

    putString("  ICMP: ");
    u32ToDec(info.icmpPackets, number, sizeof(number));
    putString(number);
    putString(" packets\n");

    Serial::writeString("[terminal] netinfo command displayed\n");
}

void Terminal::cmdNetpoll(const char* args) {
    (void)args;
    putString("\n");
    if (!network) {
        putString("Network subsystem unavailable\n");
        return;
    }
    network->poll();
    putString("Network RX queue polled\n");
}

void Terminal::cmdArping(const char* args) {
    putString("\n");
    if (!network) {
        putString("Network subsystem unavailable\n");
        return;
    }
    uint32_t ip;
    if (!network->parseIp(args, &ip)) {
        putString("Usage: arping 10.0.2.2\n");
        return;
    }
    putString("ARP request sent, waiting for reply...\n");
    bool ok = network->arping(ip);
    putString(ok ? "ARP reply received\n" : "ARP timeout\n");
}

void Terminal::cmdPing(const char* args) {
    putString("\n");
    if (!network) { putString("Network subsystem unavailable\n"); return; }

    static char host[96];
    int i = 0; while (args[i] == ' ') i++;
    int h = 0; while (args[i] != ' ' && args[i] != '\0' && h < 95) host[h++] = args[i++]; host[h] = '\0';
    if (host[0] == '\0') { putString("Usage: ping 10.0.2.2\n"); return; }

    uint32_t ip = 0;
    if (!network->parseIp(host, &ip)) {
        putString("Resolving "); putString(host); putString("...\n");
        if (!network->resolveDnsA(host, &ip)) { putString("DNS lookup failed\n"); return; }
    }
    static char ipText[16];
    network->formatIp(ip, ipText, sizeof(ipText));
    putString("PING "); putString(host); putString(" ("); putString(ipText); putString(") 16 bytes of data.\n");

    uint32_t transmitted = 0, received = 0, minMs = 0xffffffffu, maxMs = 0, sumMs = 0;
    for (uint16_t seq = 1; seq <= 4; seq++) {
        transmitted++;
        PingResult result;
        uint32_t startMs = timerMillis();
        bool ok = network->pingOnce(ip, seq, startMs, &result);
        if (ok && result.received) {
            received++;
            uint32_t ms = result.elapsedMs;
            if (ms < minMs) minMs = ms;
            if (ms > maxMs) maxMs = ms;
            sumMs += ms;
            static char from[16]; static char num[16];
            network->formatIp(result.fromIp ? result.fromIp : ip, from, sizeof(from));
            putString("16 bytes from "); putString(from); putString(": icmp_seq=");
            u32ToDec(seq, num, sizeof(num)); putString(num);
            putString(" ttl="); u32ToDec(result.ttl, num, sizeof(num)); putString(num);
            putString(" time="); u32ToDec(ms, num, sizeof(num)); putString(num); putString(" ms\n");
        } else {
            static char num[16];
            putString("Request timeout for icmp_seq "); u32ToDec(seq, num, sizeof(num)); putString(num); putString("\n");
        }
        pitSleepMs(250);
    }

    static char num[16];
    putString("--- "); putString(host); putString(" ping statistics ---\n");
    u32ToDec(transmitted, num, sizeof(num)); putString(num); putString(" packets transmitted, ");
    u32ToDec(received, num, sizeof(num)); putString(num); putString(" received, ");
    uint32_t loss = transmitted ? ((transmitted - received) * 100u) / transmitted : 0;
    u32ToDec(loss, num, sizeof(num)); putString(num); putString("% packet loss\n");
    if (received) {
        putString("rtt min/avg/max = ");
        u32ToDec(minMs, num, sizeof(num)); putString(num); putString("/");
        u32ToDec(sumMs / received, num, sizeof(num)); putString(num); putString("/");
        u32ToDec(maxMs, num, sizeof(num)); putString(num); putString(" ms\n");
    }
}

void Terminal::cmdDhcp(const char* args) {
    (void)args;
    putString("\n");
    if (!network) { putString("Network subsystem unavailable\n"); return; }
    putString("DHCP: started discover/request in the background. You can keep typing.\n");
    bool ok = network->startDhcp();
    if (!ok) { putString("DHCP could not start; NIC is unavailable or TX failed\n"); lastDhcpState = network->getDhcpState(); return; }
    lastDhcpState = network->getDhcpState();
}

void Terminal::cmdTraceroute(const char* args) {
    putString("\n");
    if (!network) { putString("Network subsystem unavailable\n"); return; }
    static char host[96];
    int i = 0; while (args[i] == ' ') i++;
    int h = 0; while (args[i] != ' ' && args[i] != '\0' && h < 95) host[h++] = args[i++]; host[h] = '\0';
    if (host[0] == '\0') { putString("Usage: traceroute example.com\n"); return; }
    uint32_t ip = 0;
    if (!network->parseIp(host, &ip)) {
        putString("Resolving "); putString(host); putString("...\n");
        if (!network->resolveDnsA(host, &ip)) { putString("DNS lookup failed\n"); return; }
    }
    static char ipText[16]; static char num[16];
    network->formatIp(ip, ipText, sizeof(ipText));
    putString("traceroute to "); putString(host); putString(" ("); putString(ipText); putString("), 8 hops max\n");
    for (uint8_t ttl = 1; ttl <= 8; ttl++) {
        TraceHopResult hop;
        uint32_t startMs = timerMillis();
        bool ok = network->tracerouteHop(ip, ttl, static_cast<uint16_t>(100 + ttl), startMs, &hop);
        u32ToDec(ttl, num, sizeof(num)); putString(num); putString("  ");
        if (ok && hop.received) {
            uint32_t shownIp = hop.fromIp ? hop.fromIp : (hop.destinationReached ? ip : hop.fromIp);
            network->formatIp(shownIp, ipText, sizeof(ipText)); putString(ipText); putString("  ");
            u32ToDec(hop.elapsedMs, num, sizeof(num)); putString(num); putString(" ms");
            if (hop.destinationReached) { putString("  reached\n"); break; }
            putString("\n");
        } else {
            putString("*\n");
        }
        pitSleepMs(150);
    }
}

void Terminal::cmdUdp(const char* args) {
    putString("\n");
    if (!network) {
        putString("Network subsystem unavailable\n");
        return;
    }

    static char ipText[32];
    static char message[96];
    int i = 0;
    while (args[i] == ' ') i++;
    int p = 0;
    while (args[i] != ' ' && args[i] != '\0' && p < 31) ipText[p++] = args[i++];
    ipText[p] = '\0';
    while (args[i] == ' ') i++;
    int m = 0;
    while (args[i] != '\0' && m < 95) message[m++] = args[i++];
    message[m] = '\0';

    uint32_t ip;
    if (ipText[0] == '\0' || message[0] == '\0') {
        putString("Usage: udp 10.0.2.2 hello\n");
        return;
    }

    if (!network->parseIp(ipText, &ip)) {
        putString("Resolving ");
        putString(ipText);
        putString("...\n");
        if (!network->resolveDnsA(ipText, &ip)) {
            putString("DNS lookup failed\n");
            return;
        }
        char resolved[16];
        network->formatIp(ip, resolved, sizeof(resolved));
        putString("Resolved to ");
        putString(resolved);
        putString("\n");
    }

    putString("Sending UDP text to ");
    putString(ipText);
    putString(":9001...\n");
    bool ok = network->sendUdpText(ip, 9001, message);
    putString(ok ? "UDP packet sent\n" : "UDP send failed\n");
}

void Terminal::cmdDns(const char* args) {
    putString("\n");
    if (!network) {
        putString("Network subsystem unavailable\n");
        return;
    }

    static char hostname[128];
    int i = 0;
    while (args[i] == ' ') i++;
    int h = 0;
    while (args[i] != ' ' && args[i] != '\0' && h < 127) hostname[h++] = args[i++];
    hostname[h] = '\0';
    if (hostname[0] == '\0') {
        putString("Usage: dns example.com\n");
        return;
    }

    char dnsText[16];
    network->formatIp(network->getInfo().dnsIp, dnsText, sizeof(dnsText));
    putString("Resolving ");
    putString(hostname);
    putString(" using DNS ");
    putString(dnsText);
    putString("...\n");
    uint32_t ip = 0;
    bool ok = network->resolveDnsA(hostname, &ip);
    if (ok) {
        char ipText[16];
        network->formatIp(ip, ipText, sizeof(ipText));
        putString("A ");
        putString(hostname);
        putString(" = ");
        putString(ipText);
        putString("\n");
    } else {
        putString("DNS lookup failed or timed out\n");
    }
}

void Terminal::cmdTcp(const char* args) {
    putString("\n");
    if (!network) { putString("Network subsystem unavailable\n"); return; }

    static char hostText[64]; static char portText[8]; static char message[128];
    int i = 0; while (args[i] == ' ') i++;
    int h = 0; while (args[i] != ' ' && args[i] != '\0' && h < 63) hostText[h++] = args[i++]; hostText[h] = '\0';
    while (args[i] == ' ') i++;
    int p = 0; while (args[i] != ' ' && args[i] != '\0' && p < 7) portText[p++] = args[i++]; portText[p] = '\0';
    while (args[i] == ' ') i++;
    int m = 0; while (args[i] != '\0' && m < 127) message[m++] = args[i++]; message[m] = '\0';
    if (hostText[0] == '\0' || portText[0] == '\0' || message[0] == '\0') { putString("Usage: tcp 1.1.1.1 80 hello\n"); return; }

    uint32_t ip = 0;
    if (!network->parseIp(hostText, &ip)) {
        putString("Resolving "); putString(hostText); putString("...\n");
        if (!network->resolveDnsA(hostText, &ip)) { putString("DNS lookup failed\n"); return; }
    }
    uint32_t port = 0;
    for (int j = 0; portText[j]; j++) { if (portText[j] < '0' || portText[j] > '9') { putString("TCP port must be numeric\n"); return; } port = port * 10 + static_cast<uint32_t>(portText[j] - '0'); }
    if (port == 0 || port > 65535) { putString("TCP port must be 1..65535\n"); return; }

    putString("TCP connect/send to "); putString(hostText); putString(":"); putString(portText); putString("...\n");
    bool ok = network->sendTcpText(ip, static_cast<uint16_t>(port), message);
    putString(ok ? "TCP text sent\n" : "TCP connect/send failed\n");
}

void Terminal::cmdHttp(const char* args) {
    putString("\n");
    if (!network) { putString("Network subsystem unavailable\n"); return; }
    static char host[64]; static char req[180]; static char resp[512];
    int i = 0; while (args[i] == ' ') i++;
    int h = 0; while (args[i] != ' ' && args[i] != '\0' && h < 63) host[h++] = args[i++]; host[h] = '\0';
    if (host[0] == '\0') { putString("Usage: http example.com\n"); return; }
    uint32_t ip = 0;
    if (!network->parseIp(host, &ip)) {
        putString("Resolving "); putString(host); putString("...\n");
        if (!network->resolveDnsA(host, &ip)) { putString("DNS lookup failed\n"); return; }
    }
    int r = 0; const char* a = "GET / HTTP/1.0\r\nHost: ";
    for (int j = 0; a[j] && r < 179; j++) req[r++] = a[j];
    for (int j = 0; host[j] && r < 179; j++) req[r++] = host[j];
    const char* b = "\r\nConnection: close\r\n\r\n";
    for (int j = 0; b[j] && r < 179; j++) req[r++] = b[j];
    req[r] = 0;
    putString("HTTP GET http://"); putString(host); putString("/ ...\n");
    bool ok = network->tcpRequestText(ip, 80, req, resp, sizeof(resp));
    if (!ok) { putString("HTTP TCP request failed\n"); return; }
    if (resp[0]) { putString(resp); putString("\n"); }
    else putString("Connected, but no HTTP data received\n");
}

void Terminal::cmdSecureChat(const char* args) {
    putString("\nSecureChat onion-only control\n");
    putString("  Policy: onion-only, no clearnet fallback\n");
    putString("  App E2E crypto: required before message send\n");

    if (!network) {
        putString("  Network: unavailable\n");
        putString("  Status: blocked\n");
        return;
    }

    const NetworkInfo& info = network->getInfo();
    putString("  Network: ");
    if (info.rtl8139Present && info.rxEnabled) {
        putString("ready\n");
    } else {
        putString("not ready\n");
        putString("  Status: blocked until Ethernet is online\n");
        return;
    }

    if (args[0] == '\0' || strcmp(args, "status") == 0) {
        putString("  Tor: not implemented/bootstrapped yet\n");
        putString("  Status: blocked until native Tor is available\n");
        putString("  Next: implement DHCP, stronger TCP, entropy/time, then Tor bootstrap\n");
        return;
    }

    if (strcmp(args, "start") == 0) {
        putString("  Start request denied safely\n");
        putString("  Reason: Tor subsystem is not bootstrapped\n");
        putString("  No direct TCP/DNS fallback was attempted\n");
        return;
    }

    putString("  Usage:\n");
    putString("    securechat status\n");
    putString("    securechat start\n");
}
