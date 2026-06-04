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

    // Linear framebuffer (used when GRUB/UEFI provides a GOP framebuffer)
    volatile unsigned char* fb_base  = 0;
    unsigned int            fb_pitch  = 0;
    unsigned int            fb_width  = 0;
    unsigned int            fb_height = 0;
    unsigned char           fb_bpp    = 0;
    bool                    use_fb    = false;

    void fb_put_pixel(int px, int py, unsigned int rgb);
    void fb_draw_char(char c, int col, int row);

    public:
    // Constructor
    Vga();

    // Switch to linear framebuffer mode (called after Multiboot2 detection)
    void init_fb(unsigned int addr, unsigned int pitch,
                 unsigned int width, unsigned int height, unsigned char bpp);

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