#ifndef _LIBC_TERMINAL_H
#define _LIBC_TERMINAL_H

#include <stddef.h>
#include <vga.hpp>
#include <interrupts.hpp>
#include <filesystem.hpp>

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
    char inputBuffer[256];
    int inputPosition;
    bool readingInput;
    volatile bool commandReady;
    
    // Command handling helper methods
    void cmdMkdir(const char* args);
    void cmdLs(const char* args);
    void cmdCd(const char* args);
    void cmdCp(const char* args);
    void cmdMv(const char* args);
    void cmdTouch(const char* args);
    void cmdCat(const char* args);
    void cmdEcho(const char* args);
    
    public:
    // Constructor
    Terminal();
    
    // Terminal methods
    void clear();
    void init(Vga* vga, Interrupts* interrupts, FileSystem* filesystem);
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