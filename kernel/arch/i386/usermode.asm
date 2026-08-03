BITS 32
section .text

global enter_user_mode
global kernel_resume

extern sched_user_enter
extern sched_user_exit

section .bss
saved_esp: resd 1
saved_ret: resd 1

section .text

enter_user_mode:
    mov eax, [esp + 4]
    mov ecx, [esp + 8]
    mov edx, [esp + 12]
    mov ebx, [esp + 16]
    mov esi, [esp + 20]

    mov edi, [esp]
    mov [saved_ret], edi
    mov [saved_esp], esp

    call sched_user_enter

    push eax
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    pop eax

    push ebx
    push ecx
    push esi
    push edx
    push eax

    iret

kernel_resume:
    cli
    call sched_user_exit
    mov esp, [saved_esp]
    ret
