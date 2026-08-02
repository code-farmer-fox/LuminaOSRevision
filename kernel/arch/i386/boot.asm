BITS 16
section .text

global _start
extern kernel_main, bss_start, bss_end

_start:
    mov [boot_drive], dl

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    call enable_a20

    lgdt [gdt_desc]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:protected_mode

BITS 32
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x20000

    mov edi, bss_start
    mov ecx, bss_end
    sub ecx, edi
    xor al, al
    rep stosb

    mov edi, 0xB8000
    mov ecx, 2000
    mov ax, 0x0720
    rep stosw

    xor ebp, ebp
    movzx eax, byte [boot_drive]
    push eax
    call kernel_main
    add esp, 4

    cli
    hlt

BITS 16
enable_a20:
    push ax

    mov ax, 0x2401
    int 0x15
    jnc .done

    call .wait_cmd
    mov al, 0xD1
    out 0x64, al
    call .wait_cmd
    mov al, 0xDF
    out 0x60, al
    call .wait_data

.done:
    pop ax
    ret

.wait_cmd:
    in al, 0x64
    test al, 2
    jnz .wait_cmd
    ret

.wait_data:
    in al, 0x64
    test al, 1
    jz .wait_data
    ret

align 4
gdt:
    dq 0
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF
    db 0x00
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92
    db 0xCF
    db 0x00
gdt_end:

gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

boot_drive: db 0
