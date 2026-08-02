BITS 32
section .text

extern syscall_dispatch

global syscall_asm

syscall_asm:
    push ds
    push es
    push fs
    push gs
    pushad

    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    pop eax

    push esi
    push edx
    push ecx
    push ebx
    push eax
    call syscall_dispatch
    add esp, 20

    mov [esp + 28], eax

    popad
    pop gs
    pop fs
    pop es
    pop ds
    iret
