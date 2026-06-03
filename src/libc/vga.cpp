#include <vga.hpp>
#include <io.hpp>

const int VGA_WIDTH = 80;
const int VGA_HEIGHT = 25;
const int VGA_DEFAULT_COLOR = 0x0F;

enum VgaColor {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};


// Constructor
Vga::Vga() {
    x = 0;
    y = 0;
    color = VGA_DEFAULT_COLOR;
    cursor_enabled = true;
    clear();
}

// Clear the screen
void Vga::clear() {
    // Use volatile writes and a single pass clear to avoid mis-optimization
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        vga_buffer[i] = (unsigned short)(' ' | (color << 8));
    }
    x = 0;
    y = 0;
}

// Put a character at the current position
void Vga::putChar(char c) {
    // Handle special characters
    if (c == '\n') {
        x = 0;
        y++;
        // Ensure we scroll when reaching the bottom via newline
        if (y >= VGA_HEIGHT) {
            scroll();
        }
        if (cursor_enabled) update_cursor();
        return; // Don't display the newline character
    }
    
    // Skip null characters to avoid displaying rectangles
    if (c == '\0') {
        return;
    }
    
    // Handle line wrapping
    if (x >= VGA_WIDTH) {
        x = 0;
        y++;
    }

    // Handle scrolling if needed
    if (y >= VGA_HEIGHT) {
        scroll();
    }
    
    // Regular character
    int index = y * VGA_WIDTH + x;
    vga_buffer[index] = (unsigned short)(c | (color << 8));
    x++;
    if (cursor_enabled) update_cursor();
}

// Put a char at x, y on screen
void Vga::putCharAt(char c, int x, int y){
    // Treat null as space to avoid odd glyphs
    if (c == '\0') {
        c = ' ';
    }

    // Clamp to screen bounds to avoid buffer underruns/overruns
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= VGA_WIDTH) x = VGA_WIDTH - 1;
    if (y >= VGA_HEIGHT) y = VGA_HEIGHT - 1;

    int index = y * VGA_WIDTH + x;
    vga_buffer[index] = (unsigned short)(c | (color << 8));
    if (cursor_enabled) update_cursor();
}

// Get current X position
int Vga::get_x() {
    return x;
}

// Get current Y position
int Vga::get_y() {
    return y;
}

// Set cursor position
void Vga::set_xy(int new_x, int new_y) {
    // Enforce bounds
    if (new_x >= 0 && new_x < VGA_WIDTH && new_y >= 0 && new_y < VGA_HEIGHT) {
        x = new_x;
        y = new_y;
    }
    if (cursor_enabled) update_cursor();
}

// Scroll the screen up one line
void Vga::scroll() {
    // Move all lines up one position
    for (int y = 0; y < VGA_HEIGHT - 1; ++y) {
        for (int x = 0; x < VGA_WIDTH; ++x) {
            int to_index = y * VGA_WIDTH + x;
            int from_index = (y + 1) * VGA_WIDTH + x;
            vga_buffer[to_index] = vga_buffer[from_index];
        }
    }

    // Clear the last line
    int last_line_offset = (VGA_HEIGHT - 1) * VGA_WIDTH;
    for (int x = 0; x < VGA_WIDTH; ++x) {
        vga_buffer[last_line_offset + x] = (unsigned short)(' ' | (color << 8));
    }

    // Move cursor to start of last line
    x = 0;
    y = VGA_HEIGHT - 1;
}

// Set text color
void Vga::set_color(int new_color) {
    color = new_color;
}

// Update cursor follow inputs
void Vga::update_cursor() {
    if (!cursor_enabled) return;
    unsigned short pos = y * 80 + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

void Vga::set_cursor_enabled(bool enabled) {
    cursor_enabled = enabled;
}

void Vga::force_update_cursor() {
    bool prev = cursor_enabled;
    cursor_enabled = true;
    // Directly update cursor position
    unsigned short pos = y * 80 + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
    cursor_enabled = prev;
}