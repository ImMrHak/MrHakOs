#include <vga.hpp>

// VGA constants
const int VGA_WIDTH = 80;
const int VGA_HEIGHT = 25;
const int VGA_DEFAULT_COLOR = 0x0F; // White on black

// Constructor
Vga::Vga() {
    x = 0;
    y = 0;
    color = VGA_DEFAULT_COLOR;
    clear();
}

// Clear the screen
void Vga::clear() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        vga_buffer[i] = (' ' | (color << 8));
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
    vga_buffer[index] = (c | (color << 8));
    x++;
}

// Put a char at x, y on screen
void Vga::putCharAt(char c, int x, int y){
    if(x >= VGA_WIDTH){
        x = 0;
        y++;
    }
    if(y >= VGA_HEIGHT){
        scroll();
    }
    int index = y * VGA_WIDTH + x;
    vga_buffer[index] = (c | (color << 8));
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
        vga_buffer[last_line_offset + x] = (' ' | (color << 8));
    }

    // Move cursor to start of last line
    x = 0;
    y = VGA_HEIGHT - 1;
}

// Set text color
void Vga::set_color(int new_color) {
    color = new_color;
}