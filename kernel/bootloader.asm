; bootloader_fixed.asm
; Assemble: nasm -f bin bootloader_fixed.asm -o bootloader_fixed.bin
[BITS 16]
[ORG 0x7C00]

%define KERNEL_LOAD_ADDR 0x00010000   ; 64 KiB
%define KERNEL_SECTORS   40           ; ajuster selon taille réelle
%define DISK             0x80
%define VESA_MODE        0x118
%define DAP_ADDR         0x7C80
%define VBE_INFO_ADDR    0x07A00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov si, boot_msg
    call print_string

    ; --- Obtenir infos VBE ---
    mov ax, 0x4F01
    mov cx, VESA_MODE
    mov di, VBE_INFO_ADDR
    int 0x10

    ; --- Activer le mode VESA ---
    mov ax, 0x4F02
    mov bx, 0x4000 | VESA_MODE
    int 0x10
    cmp ax, 0x004F
    jne .no_vesa
    mov si, vesa_ok_msg
    call print_string
    jmp .load_kernel
.no_vesa:
    mov si, vesa_fail_msg
    call print_string

.load_kernel:
    ; --- Construire DAP ---
    xor ax, ax
    mov ds, ax
    mov si, DAP_ADDR

    mov byte [si+0], 0x10
    mov byte [si+1], 0
    mov word [si+2], KERNEL_SECTORS
    mov word [si+4], 0x0000
    mov word [si+6], (KERNEL_LOAD_ADDR >> 4)
    mov dword [si+8], 1
    mov dword [si+12], 0

    mov ah, 0x42
    mov dl, DISK
    int 0x13
    jc .disk_error

    mov si, load_ok_msg
    call print_string

    ; --- GDT ---
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_entry

[BITS 32]
protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    mov eax, VBE_INFO_ADDR
    push dword 0x00010000
    push dword 0x08
    retf

.disk_error:
    mov si, disk_error_msg
    call print_string
    cli
    hlt
    jmp .disk_error

print_string:
    pusha
.print_loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp .print_loop
.done:
    popa
    ret

; ---------------- GDT ----------------
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

boot_msg        db "Bootloader: demarrage...",0
vesa_ok_msg     db "VESA ok",0
vesa_fail_msg   db "VESA fail",0
load_ok_msg     db "Kernel charge a 0x10000",0
disk_error_msg  db "Erreur lecture disque",0

times 510 - ($-$$) db 0
dw 0xAA55
