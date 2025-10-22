; bootloader_debug_fixed.asm
; Assemble: nasm -f bin bootloader_debug_fixed.asm -o bootloader_debug_fixed.bin
[BITS 16]
[ORG 0x7C00]

%define KERNEL_LOAD_ADDR 0x00010000   ; 64 KiB (linker)
%define DISK             0x80
%define VESA_MODE        0x118
%define DAP_ADDR         0x0200       ; éviter d’écraser le bootloader
%define VBE_INFO_ADDR    0x07A00
%define KERNEL_SECTORS   16250
%define CHUNK_MAX        127

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov si, boot_msg
    call print_string

    ; --- VBE ---
    mov ax, 0x4F01
    mov cx, VESA_MODE
    mov di, VBE_INFO_ADDR
    int 0x10

    mov ax, 0x4F02
    mov bx, 0x4000 | VESA_MODE
    int 0x10
    cmp ax, 0x004F
    jne .vesa_fail
    mov si, vesa_ok
    call print_string
    jmp .after_vesa
.vesa_fail:
    mov si, vesa_fail
    call print_string
.after_vesa:

    call enable_a20_port92

    xor ax, ax
    mov ds, ax

    mov dword [total_read], 0
    mov dword [remaining], KERNEL_SECTORS
    mov dword [lba_low], 1
    mov dword [buffer_phys], KERNEL_LOAD_ADDR

.read_loop:
    cmp dword [remaining], 0
    je .done_loading

    mov eax, [remaining]
    cmp eax, CHUNK_MAX
    jle .use_remaining
    mov eax, CHUNK_MAX
.use_remaining:
    mov [chunk_size], eax

    mov si, DAP_ADDR
    mov byte [si+0], 0x10
    mov byte [si+1], 0x00
    mov word [si+2], ax

    mov eax, [buffer_phys]
    mov ebx, eax
    and ebx, 0x0000000F
    mov word [si+4], bx
    shr eax, 4
    mov word [si+6], ax

    mov eax, [lba_low]
    mov [si+8], eax
    mov dword [si+12], 0

    mov ah, 0x42
    mov dl, DISK
    mov si, DAP_ADDR
    int 0x13
    jc disk_fail

    mov eax, [remaining]
    sub eax, [chunk_size]
    mov [remaining], eax

    mov eax, [lba_low]
    add eax, [chunk_size]
    mov [lba_low], eax

    mov eax, [chunk_size]
    mov ecx, 512
    mul ecx
    add [buffer_phys], eax

    mov eax, [total_read]
    add eax, [chunk_size]
    mov [total_read], eax

    jmp .read_loop

.done_loading:
    mov si, load_ok
    call print_string

    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:pm_entry

[BITS 32]
pm_entry:
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

disk_fail:
    mov si, disk_err
    call print_string
    cli
    hlt
    jmp disk_fail

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

enable_a20_port92:
    in al, 0x92
    or al, 00000010b
    out 0x92, al
    ret

gdt_start:
    dw 0x0000, 0x0000
    db 0x00, 0x00, 0x00, 0x00

    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00

    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

boot_msg      db "Bootloader: demarrage...",0
vesa_ok       db "VESA ok",0
vesa_fail     db "VESA non dispo",0
load_ok       db "Kernel charge a 0x10000",0
disk_err      db "Erreur lecture disque",0

chunk_size   dd 0
total_read   dd 0
remaining    dd 0
lba_low      dd 0
buffer_phys  dd 0

times 510 - ($ - $$) db 0
dw 0xAA55
