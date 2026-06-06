#ifndef _LIBC_TERMINAL_H
#define _LIBC_TERMINAL_H

#include <stddef.h>
#include <vga.hpp>
#include <interrupts.hpp>
#include <filesystem.hpp>
#include <network.hpp>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration for keyboard handler
void terminal_keyboard_handler();

class Terminal{
    private:
    Vga* vga;
    Interrupts* interrupts;
    FileSystem* filesystem;
    Network* network;
    char inputBuffer[256];
    int inputPosition;          // logical length of the line
    int cursorPos;              // edit caret within [0, inputPosition]
    int inputStartX;            // screen column where the input begins
    int inputStartY;            // screen row where the input begins
    bool readingInput;
    volatile bool commandReady;

    // Command history for Up/Down recall. The backing line buffer lives in .bss
    // (see terminal.cpp) to keep this stack-allocated object small; the 64-bit
    // boot stack sits just above the page tables and has little headroom.
    int historyCount;           // number of stored entries
    int historyIndex;           // browse cursor; == historyCount means "new line"
    uint8_t lastDhcpState;
    bool torDirectoryReachable;
    bool torCircuitsReady;
    bool torTlsReady;
    bool torTlsHandshakeSeen;
    uint16_t torTlsRxLen;
    uint8_t torTlsRecordType;
    uint8_t torTlsMajor;
    uint8_t torTlsMinor;
    uint16_t torTlsRecordLen;
    uint8_t torTlsHandshakeType;
    uint32_t torTlsHandshakeLen;
    uint8_t torTlsAlertLevel;
    uint8_t torTlsAlertDescription;
    uint32_t torRelayCount;
    uint32_t torGuardCount;
    uint32_t torExitCount;
    uint32_t torFastCount;
    uint32_t torStableCount;
    uint32_t torRunningCount;
    uint32_t torValidCount;
    uint32_t torUsableGuardCount;
    char torSelectedNickname[32];
    char torSelectedIp[16];
    uint32_t torSelectedOrPort;
    bool torSelectedHasFast;
    bool torSelectedHasStable;
    bool torSelectedHasRunning;
    bool torSelectedHasValid;
    bool torSelectedHasExit;
    
    // Command handling helper methods
    void cmdMkdir(const char* args);
    void cmdLs(const char* args);
    void cmdCd(const char* args);
    void cmdCp(const char* args);
    void cmdMv(const char* args);
    void cmdTouch(const char* args);
    void cmdCat(const char* args);
    void cmdEcho(const char* args);
    void cmdNetinfo(const char* args);
    void cmdMeminfo(const char* args);
    void cmdNetpoll(const char* args);
    void cmdArping(const char* args);
    void cmdPing(const char* args);
    void cmdDhcp(const char* args);
    void cmdTraceroute(const char* args);
    void cmdUdp(const char* args);
    void cmdDns(const char* args);
    void cmdTcp(const char* args);
    void cmdHttp(const char* args);
    void cmdCurl(const char* args);
    void cmdSocks5(const char* args);
    void cmdTor(const char* args);
    void cmdSecureChat(const char* args);
    
    public:
    // Constructor
    Terminal();
    
    // Terminal methods
    void clear();
    void init(Vga* vga, Interrupts* interrupts, FileSystem* filesystem, Network* network);
    void run();
    void putChar(char c);
    void putString(const char* str);
    void handleKeypress();
    void handleChar(char key);
    void processCommand(const char* cmd);
    void showPrompt();

    // Line-editing + history helpers
    void redrawFrom(int from);
    void placeCursorAt(int pos);
    void setInputLine(const char* text);
    void historyAdd(const char* cmd);
    
    // Keyboard handler callback (needs to be called from static function)
    void onKeypress();
};

// Global terminal instance for keyboard handler
extern Terminal* g_terminal;

#ifdef __cplusplus
}
#endif

#endif // _LIBC_TERMINAL_H