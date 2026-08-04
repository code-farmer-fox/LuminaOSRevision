BITS 32
section .text

global enter_user_mode
global kernel_resume

extern sched_user_enter
extern sched_user_exit

section .bss
saved_esp: resd 1

section .text

enter_user_mode:
    mov edi, [esp + 4]
    mov ecx, [esp + 8]
    mov edx, [esp + 12]
    mov ebx, [esp + 16]
    mov esi, [esp + 20]

    mov [saved_esp], esp

    push edi
    call sched_user_enter
    pop edi

    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push ebx
    push ecx
    push esi
    push edx
    push edi

    iret

kernel_resume:
    pushfd
    pop eax
    cli
    call sched_user_exit
    mov esp, [saved_esp]
    push eax
    popfd
    ret
