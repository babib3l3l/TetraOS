OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(start)

MEMORY {
    ram (rwx) : ORIGIN = 0x00010000, LENGTH = 64M
}

SECTIONS {
    . = 0x00010000;

    /* Section start ABSOLUMENT en premier */
    .text.start : {
        *(.text.start)      /* start() ici */
    } >ram

    /* Le reste du code */
    .text : {
        *(.text)            /* Tout le code SAUF start() */
    } >ram

    /* Données read-only */
    .rodata : {
        *(.rodata*)
    } >ram

    .data : { *(.data) } >ram
    .bss : { *(.bss) } >ram

    _kernel_start = ADDR(.text.start);
    _kernel_end = .;

    /DISCARD/ : {
        *(.comment) *(.note) *(.eh_frame)
    }
}