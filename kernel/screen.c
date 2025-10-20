#include "screen.h"
#include "boot_info.h"
#include <stdint.h>
#include "utils.h"
#include "image_data.h"

#define SCREEN_WIDTH  80
#define SCREEN_HEIGHT 25
#define VIDEO_MEMORY  0xB8000
#define DEFAULT_COLOR 0x0F  // Blanc sur noir

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

int cursor_row = 0;
int cursor_col = 0;

/* Fallback text mode functions (VGA text) */
static void vga_putc(char c);

/* Graphics mode: simple background blit */
static int graphics_mode = 0;
static uint8_t *framebuffer = 0;
static int fb_width = 0;
static int fb_height = 0;
static int fb_pitch = 0;
static int fb_bpp = 0;

void screen_init(void) {
    // detect boot_info signature
    volatile boot_info_t *bi = (volatile boot_info_t *) BOOT_INFO_ADDR;
    if (bi->signature == BOOT_SIGNATURE) {
        // graphics available
        framebuffer = (uint8_t*)(bi->phys_base);
        fb_width = bi->fb_width;
        fb_height = bi->fb_height;
        fb_pitch = bi->fb_pitch;
        fb_bpp = bi->fb_bpp;
        if (fb_bpp == 32) {
            graphics_mode = 1;
            // blit background (assuming BGRA order and pitch = width*4)
            int copy_h = (fb_height < BACKGROUND_HEIGHT) ? fb_height : BACKGROUND_HEIGHT;
            int copy_w = (fb_width < BACKGROUND_WIDTH) ? fb_width : BACKGROUND_WIDTH;
            for (int y = 0; y < copy_h; y++) {
                uint8_t *dst = framebuffer + y * fb_pitch;
                const unsigned char *src = background_bgra + y * BACKGROUND_WIDTH * 4;
                memcpy(dst, src, copy_w * 4);
            }
            return;
        }
    }
    // Fallback: VGA text init
    cursor_row = 0;
    cursor_col = 0;
}

void print_char(char c) {
    if (graphics_mode) return; // no text printing in graphics for now

    volatile char* video = (char*) VIDEO_MEMORY;

    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= SCREEN_HEIGHT) {
            // scroll
            for (int r = 0; r < SCREEN_HEIGHT-1; r++) {
                for (int c = 0; c < SCREEN_WIDTH; c++) {
                    int off_src = 2 * (r * SCREEN_WIDTH + c);
                    int off_dst = 2 * ((r+1) * SCREEN_WIDTH + c);
                    video[off_dst] = video[off_src];
                    video[off_dst+1] = video[off_src+1];
                }
            }
            // clear first line
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                int off = 2 * x;
                video[off] = ' ';
                video[off+1] = DEFAULT_COLOR;
            }
            cursor_row = SCREEN_HEIGHT - 1;
        }
        return;
    }

    if (c == '\r') {
        cursor_col = 0;
        return;
    }

    if (c == '\b') {
        if (cursor_col > 0) cursor_col--;
        int off = 2 * (cursor_row * SCREEN_WIDTH + cursor_col);
        video[off] = ' ';
        video[off+1] = DEFAULT_COLOR;
        return;
    }

    int off = 2 * (cursor_row * SCREEN_WIDTH + cursor_col);
    video[off] = c;
    video[off+1] = DEFAULT_COLOR;
    cursor_col++;
    if (cursor_col >= SCREEN_WIDTH) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= SCREEN_HEIGHT) cursor_row = SCREEN_HEIGHT - 1;
    }
}

void print(const char *s) {
    for (; *s; s++) print_char(*s);
}

// --- Compatibility wrappers for old text API ---

void clear_screen() {
    if (graphics_mode) {
        // Optional: fill framebuffer with black
        if (framebuffer && fb_bpp == 32) {
            memset(framebuffer, 0, fb_pitch * fb_height);
        }
        return;
    }
    volatile char *video = (char*) VIDEO_MEMORY;
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        video[i * 2] = ' ';
        video[i * 2 + 1] = DEFAULT_COLOR;
    }
    cursor_row = 0;
    cursor_col = 0;
}

void print_string(const char *str) {
    print(str);
}

void print_xy(int x, int y, const char *text) {
    if (graphics_mode) return;  // ignore in graphics mode
    cursor_row = y;
    cursor_col = x;
    print(text);
}

void set_cursor(int row, int col) {
    cursor_row = row;
    cursor_col = col;
}

void screen_fill_rect(int x, int y, int w, int h, char c) {
    if (graphics_mode) {
        // fill rectangle with color if you want, else skip
        return;
    }
    volatile char* video = (char*) VIDEO_MEMORY;
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            int off = 2 * (j * SCREEN_WIDTH + i);
            video[off] = c;
            video[off + 1] = DEFAULT_COLOR;
        }
    }
}

int screen_get_width(void) {
    return graphics_mode ? fb_width : SCREEN_WIDTH;
}

// --- Extra helper for hexadecimal printing ---
void print_hex(uint32_t num) {
    char buffer[11]; // "0x" + 8 hex digits + null
    buffer[0] = '0';
    buffer[1] = 'x';
    const char *hex = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) {
        buffer[9 - i] = hex[num & 0xF];
        num >>= 4;
    }
    buffer[10] = '\0';
    print_string(buffer);
}

// --- Helper for decimal printing ---
void print_dec(uint32_t num) {
    char buffer[11]; // max 10 digits + null
    buffer[10] = '\0';
    int i = 9;

    // cas spécial pour 0
    if (num == 0) {
        buffer[9] = '0';
        print_string(&buffer[9]);
        return;
    }

    while (num > 0 && i >= 0) {
        buffer[i] = '0' + (num % 10);
        num /= 10;
        i--;
    }

    print_string(&buffer[i + 1]);
}

