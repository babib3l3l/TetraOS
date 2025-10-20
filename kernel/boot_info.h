// kernel/bootinfo.h
#ifndef BOOTINFO_H
#define BOOTINFO_H

#define BOOT_SIGNATURE 0x544F4F42  // "BOOT"

#include <stdint.h>

typedef struct {
    uint32_t signature;      // "BOOT"
    uint32_t mem_lower_kb;   // mémoire basse en KB
    uint32_t mem_upper_kb;   // mémoire haute en KB

    uint32_t phys_base;      // framebuffer
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint8_t  fb_bpp;
    uint8_t  fb_type;        // 1 = RGB, 0 = palettisé
    uint16_t reserved;       // padding pour alignement

    uint32_t kernel_load_addr;
    uint32_t kernel_size_bytes;

    uint8_t acpi_enabled;
    uint8_t cpu_count;
    uint16_t padding2;

} boot_info_t;

#define BOOT_INFO_ADDR 0x7E00

#endif
