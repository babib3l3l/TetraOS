// screen_fb.h
#ifndef SCREEN_FB_H
#define SCREEN_FB_H

#include <stdint.h>
#include <stddef.h>

/* Initialise le framebuffer à partir de l'adresse du VBE Mode Info Block (physique). 
   Retourne 0 si OK, non-0 en erreur. */
int fb_init(uint32_t vbe_mode_info_phys_addr);

/* Résolution et format lus après init */
uint32_t fb_width(void);
uint32_t fb_height(void);
uint32_t fb_pitch(void);     /* bytes per scanline */
uint32_t fb_bpp(void);       /* bits per pixel */

/* Primitives */
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color); /* color = 0xAARRGGBB ou format selon bpp */
void fb_clear(uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

/* helper for ARGB input */
void fb_putpixel_argb(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b);

#endif
