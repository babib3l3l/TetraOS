#include "boot_info.h"
#include "mem_boot.h"

boot_info_t* boot_info = (boot_info_t*) BOOT_INFO_ADDR;

void boot_info_dump(void) {
    print_string("=== Boot Info ===\n");

    print_string("Signature: "); 
    print_hex(boot_info->signature); 
    print_string("\n");

    print_string("Memory lower: "); 
    print_dec(boot_info->mem_lower_kb); 
    print_string(" KB\n");

    print_string("Memory upper: "); 
    print_dec(boot_info->mem_upper_kb); 
    print_string(" KB\n");

    print_string("Framebuffer @ "); 
    print_hex((uint32_t)boot_info->phys_base); 
    print_string("\n");

    print_string("Size: "); 
    print_dec(boot_info->fb_width);
    print_string("x"); 
    print_dec(boot_info->fb_height); 
    print_string("\n");

    print_string("Pitch: "); 
    print_dec(boot_info->fb_pitch);
    print_string("  Bpp: "); 
    print_dec(boot_info->fb_bpp); 
    print_string("\n");

    print_string("=================\n");

    // Vérification de la signature
    if (boot_info->signature != BOOT_SIGNATURE) return;

    // Initialisation mémoire
    mem_boot_init((uintptr_t)boot_info->phys_base, (size_t)(boot_info->fb_width * boot_info->fb_height * (boot_info->fb_bpp / 8)));
}
