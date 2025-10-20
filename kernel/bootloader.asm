[BITS 16]
[ORG 0x7C00]

; === CONSTANTES ===
%define BOOT_INFO_ADDR   0x7E00
%define KERNEL_LOAD_ADDR 0x10000      ; 64 KB
%define KERNEL_SECTORS   40
%define DISK             0x80
%define VESA_MODE        0x118
%define BOOT_SIGNATURE   0x544F4F42    ; "BOOT"

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; ===============================
    ; Message d'accueil
    ; ===============================
    mov si, boot_msg
.print_msg:
    lodsb
    or al, al
    jz .after_msg
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp .print_msg
.after_msg:

    ; ===============================
    ; Mode graphique VESA
    ; ===============================
    mov ax, 0x4F02
    mov bx, VESA_MODE | 0x4000
    int 0x10
    cmp ax, 0x004F
    jne .no_vesa

    mov ax, 0x8000
    mov es, ax
    xor di, di

    mov ax, 0x4F01
    mov cx, VESA_MODE
    int 0x10
    cmp ax, 0x004F
    jne .no_vesa

    call write_boot_info

.after_vesa:
    ; ===============================
    ; Chargement du kernel
    ; ===============================
    mov si, load_msg
.print_load:
    lodsb
    or al, al
    jz .do_load
    mov ah, 0x0E
    int 0x10
    jmp .print_load
.do_load:

    ; DAP (Disk Address Packet) pour LBA
    mov bx, 0x7C80
    mov di, bx
    mov byte [di], 0x10
    mov byte [di+1], 0x00
    mov word [di+2], KERNEL_SECTORS
    mov word [di+4], (KERNEL_LOAD_ADDR & 0x0F)
    mov word [di+6], (KERNEL_LOAD_ADDR >> 4)
    mov dword [di+8], 1
    mov dword [di+12], 0

    mov si, bx
    mov ah, 0x42
    mov dl, DISK
    int 0x13
    jc .disk_error

    ; ===============================
    ; Passage en mode protégé 32 bits
    ; ===============================
    call setup_gdt
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1            ; PE = 1
    mov cr0, eax
    jmp CODE_SEG:protected_entry   ; far jump

.no_vesa:
    mov si, vesa_fail
    call print_string
    jmp .halt

.disk_error:
    mov si, disk_error
    call print_string
.halt:
    cli
    hlt
    jmp .halt

; ===============================
; Sous-routines
; ===============================
print_string:
.next_char:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp .next_char
.done:
    ret

write_boot_info:
    mov ax, BOOT_INFO_ADDR >> 4
    mov es, ax
    xor di, di

    mov eax, BOOT_SIGNATURE
    stosd
    mov eax, 640
    stosd
    mov eax, 262144
    stosd

    mov ax, 0x8000
    mov fs, ax
    mov si, 0x28
    mov eax, [fs:si]
    stosd
    mov si, 0x12
    movzx eax, word [fs:si]
    stosd
    mov si, 0x14
    movzx eax, word [fs:si]
    stosd
    mov si, 0x10
    movzx eax, word [fs:si]
    stosd
    mov si, 0x19
    mov al, [fs:si]
    stosb
    mov al, 1
    stosb
    xor ax, ax
    stosw
    mov eax, KERNEL_LOAD_ADDR
    stosd
    mov eax, KERNEL_SECTORS*512
    stosd
    mov al, 1
    stosb
    mov al, 1
    stosb
    xor ax, ax
    stosw
    ret

; ===============================
; GDT minimale (base=0 pour kernel)
; ===============================
gdt_start:
    dq 0x0000000000000000       ; null
    dq 0x00CF9A000000FFFF       ; code 32 bits, base=0, limit=4GB
    dq 0x00CF92000000FFFF       ; data 32 bits

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

gdt_end:

CODE_SEG equ 0x08
DATA_SEG equ 0x10

setup_gdt:
    ret    ; GDT statique déjà définie

protected_entry:
    ; -------------------------------
    ; Segments 32 bits
    ; -------------------------------
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000          ; stack 32 bits

    ; Jump vers kernel en offset = KERNEL_LOAD_ADDR
    jmp CODE_SEG:KERNEL_LOAD_ADDR

; ===============================
; Messages
; ===============================
boot_msg    db "Bootloader: Initialisation du mode graphique...", 0
load_msg    db "Chargement du kernel en memoire...", 0
vesa_fail   db "Echec: VESA non supporte", 0
disk_error  db "Echec: Erreur de lecture disque", 0

times 510 - ($ - $$) db 0
dw 0xAA55
