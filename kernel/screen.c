// screen.c
#include "screen.h"
#include <stdint.h>

/* Offsets in VBE ModeInfoBlock (per VBE spec) */
#define MIB_BYTES_PER_SCANLINE_OFF 16   /* word */
#define MIB_XRES_OFF               18   /* word */
#define MIB_YRES_OFF               20   /* word */
#define MIB_BPP_OFF                25   /* byte */
#define MIB_PHYS_BASE_PTR_OFF      40   /* dword */

static volatile uint8_t *fb = 0;
static uint32_t width = 0;
static uint32_t height = 0;
static uint32_t pitch = 0;
static uint32_t bits_per_pixel = 0;
static uint32_t bytes_per_pixel = 0;

static inline uint8_t read_u8(uint32_t phys) {
    volatile uint8_t *p = (volatile uint8_t *) (uintptr_t) phys;
    return *p;
}
static inline uint16_t read_u16(uint32_t phys) {
    volatile uint8_t *p = (volatile uint8_t *) (uintptr_t) phys;
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t read_u32(uint32_t phys) {
    volatile uint8_t *p = (volatile uint8_t *) (uintptr_t) phys;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int screen_init(uint32_t vbe_mode_info_phys_addr) {
    if (vbe_mode_info_phys_addr == 0) return -1;

    /* lire les champs du Mode Info Block */
    uint16_t bps = read_u16(vbe_mode_info_phys_addr + MIB_BYTES_PER_SCANLINE_OFF);
    uint16_t xres = read_u16(vbe_mode_info_phys_addr + MIB_XRES_OFF);
    uint16_t yres = read_u16(vbe_mode_info_phys_addr + MIB_YRES_OFF);
    uint8_t  bpp  = read_u8 (vbe_mode_info_phys_addr + MIB_BPP_OFF);
    uint32_t phys_fb = read_u32(vbe_mode_info_phys_addr + MIB_PHYS_BASE_PTR_OFF);

    if (xres == 0 || yres == 0 || phys_fb == 0) return -2;

    width = xres;
    height = yres;
    pitch = bps;
    bits_per_pixel = bpp;
    bytes_per_pixel = (bpp + 7) / 8;
    fb = (volatile uint8_t *)(uintptr_t)phys_fb;

    /* basic sanity checks */
    if (bytes_per_pixel == 0) return -3;
    if (pitch < width * bytes_per_pixel) {
        /* pitch improbable but toléré ; on continuera en utilisant pitch */
    }

    return 0;
}

uint32_t fb_width(void)  { return width; }
uint32_t fb_height(void) { return height; }
uint32_t fb_pitch(void)  { return pitch; }
uint32_t fb_bpp(void)    { return bits_per_pixel; }

/* conversions simples pour écrire un pixel selon bpp */
static void write_pixel_32(uint32_t x, uint32_t y, uint32_t color) {
    uint8_t *dst = (uint8_t *)fb + y * pitch + x * 4;
    dst[0] = (uint8_t)(color & 0xFF);         /* B */
    dst[1] = (uint8_t)((color >> 8) & 0xFF);  /* G */
    dst[2] = (uint8_t)((color >> 16) & 0xFF); /* R */
    dst[3] = (uint8_t)((color >> 24) & 0xFF); /* A or padding */
}
static void write_pixel_24(uint32_t x, uint32_t y, uint32_t color) {
    uint8_t *dst = (uint8_t *)fb + y * pitch + x * 3;
    dst[0] = (uint8_t)(color & 0xFF);
    dst[1] = (uint8_t)((color >> 8) & 0xFF);
    dst[2] = (uint8_t)((color >> 16) & 0xFF);
}
static void write_pixel_16(uint32_t x, uint32_t y, uint32_t color) {
    /* assume RGB565 input from color: 0x00RRGGBB -> convert */
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = (color) & 0xFF;
    uint16_t v = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
    uint8_t *dst = (uint8_t *)fb + y * pitch + x * 2;
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
}
static void write_pixel_8(uint32_t x, uint32_t y, uint32_t color) {
    uint8_t *dst = (uint8_t *)fb + y * pitch + x;
    dst[0] = (uint8_t)(color & 0xFF);
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= width || y >= height) return;
    if (!fb) return;
    switch (bytes_per_pixel) {
        case 4: write_pixel_32(x,y,color); break;
        case 3: write_pixel_24(x,y,color); break;
        case 2: write_pixel_16(x,y,color); break;
        case 1: write_pixel_8(x,y,color); break;
        default: /* unsupported */ break;
    }
}

void fb_putpixel_argb(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t col = (r << 16) | (g << 8) | b;
    fb_putpixel(x,y,col);
}

void fb_clear(uint32_t color) {
    if (!fb) return;
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t *line = (uint8_t *)fb + y * pitch;
        for (uint32_t x = 0; x < width; ++x) {
            switch (bytes_per_pixel) {
                case 4: {
                    uint32_t *p = (uint32_t *)(void *)(line + x*4);
                    *p = color;
                } break;
                case 3: {
                    uint8_t *p = line + x*3;
                    p[0] = (uint8_t)(color & 0xFF);
                    p[1] = (uint8_t)((color >> 8) & 0xFF);
                    p[2] = (uint8_t)((color >> 16) & 0xFF);
                } break;
                case 2: {
                    uint8_t *p = line + x*2;
                    uint8_t r = (color >> 16) & 0xFF;
                    uint8_t g = (color >> 8) & 0xFF;
                    uint8_t b = color & 0xFF;
                    uint16_t v = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
                    p[0] = (uint8_t)(v & 0xFF);
                    p[1] = (uint8_t)((v >> 8) & 0xFF);
                } break;
                case 1: {
                    line[x] = (uint8_t)(color & 0xFF);
                } break;
            }
        }
    }
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!fb) return;
    if (x >= width || y >= height) return;
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    for (uint32_t yy = y; yy < y + h; ++yy) {
        uint8_t *line = (uint8_t *)fb + yy * pitch;
        for (uint32_t xx = x; xx < x + w; ++xx) {
            switch (bytes_per_pixel) {
                case 4: { uint32_t *p = (uint32_t *)(void *)(line + xx*4); *p = color; } break;
                case 3: { uint8_t *p = line + xx*3; p[0] = (uint8_t)(color & 0xFF); p[1] = (uint8_t)((color >> 8) & 0xFF); p[2] = (uint8_t)((color >> 16) & 0xFF);} break;
                case 2: { uint8_t *p = line + xx*2; uint8_t r=(color>>16)&0xFF,g=(color>>8)&0xFF,b=color&0xFF; uint16_t v=(uint16_t)((r>>3)<<11| (g>>2)<<5 | (b>>3)); p[0]=(uint8_t)(v&0xFF); p[1]=(uint8_t)((v>>8)&0xFF);} break;
                case 1: { line[xx] = (uint8_t)(color & 0xFF); } break;
            }
        }
    }
}
