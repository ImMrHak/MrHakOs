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
    int inputPosition;
    bool readingInput;
    volatile bool commandReady;
    uint8_t lastDhcpState;
    
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
    void cmdNetpoll(const char* args);
    void cmdArping(const char* args);
    void cmdPing(const char* args);
    void cmdDhcp(const char* args);
    void cmdTraceroute(const char* args);
    void cmdUdp(const char* args);
    void cmdDns(const char* args);
    void cmdTcp(const char* args);
    void cmdHttp(const char* args);
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
    void processCommand(const char* cmd);
    void showPrompt();
    
    // Keyboard handler callback (needs to be called from static function)
    void onKeypress();
};

// Global terminal instance for keyboard handler
extern Terminal* g_terminal;

#ifdef __cplusplus
}
#endif

#endif // _LIBC_TERMINAL_H