#include "global.h"
#include "vga.h"

void clear_screen() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA[i] = (0x07 << 8) | ' ';
    }
    cursor = 0;
}

void print_char(char c) {
    if (c == '\n') {
        int line = cursor / VGA_WIDTH;
        cursor = (line + 1) * VGA_WIDTH;
        if (cursor >= VGA_WIDTH * VGA_HEIGHT) cursor = 0;
    } else {
        VGA[cursor++] = (0x0F << 8) | c;
        if (cursor >= VGA_WIDTH * VGA_HEIGHT) cursor = 0;
    }
}

void print_string(const char* str) {
    while (*str) print_char(*str++);
}
