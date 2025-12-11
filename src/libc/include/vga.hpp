#ifndef _LIBC_VGA_H
#define _LIBC_VGA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

class Vga{
    private:
    volatile unsigned short* vga_buffer = (unsigned short*)0xB8000;
    int x, y;  
    int color;
    bool cursor_enabled;

    public:
    // Constructor
    Vga();

    // VGA methods
    void clear();
    void putChar(char c);
    void putCharAt(char c, int x, int y);
    int get_x();
    int get_y();
    void set_xy(int x, int y);
    void scroll();
    void set_color(int color);
    void update_cursor();
    void set_cursor_enabled(bool enabled);
    void force_update_cursor();
};

#ifdef __cplusplus
}
#endif

#endif // _LIBC_VGA_H