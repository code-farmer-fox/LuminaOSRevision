#include <kernel/idt.h>
#include <stdint.h>

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

static void isr_hex(uint8_t v)
{
    char c = (v < 10) ? (char)('0' + v) : (char)('A' + v - 10);
    __asm__ volatile ("outb %0, $0xE9" :: "a"((uint8_t)c));
}

static void isr_hex32(uint32_t v)
{
    isr_hex((v >> 28) & 0xF);
    isr_hex((v >> 24) & 0xF);
    isr_hex((v >> 20) & 0xF);
    isr_hex((v >> 16) & 0xF);
    isr_hex((v >> 12) & 0xF);
    isr_hex((v >> 8) & 0xF);
    isr_hex((v >> 4) & 0xF);
    isr_hex(v & 0xF);
}

void isr_handler(uint32_t vector, uint32_t error, uint32_t eip, uint32_t* regs)
{
    uint32_t user_esp = regs[14];
    uint32_t ecx = regs[2];
    __asm__ volatile ("outb %0, $0xE9" :: "a"((uint8_t)'E'));
    __asm__ volatile ("outb %0, $0xE9" :: "a"((uint8_t)'x'));
    isr_hex((vector >> 4) & 0xF);
    isr_hex(vector & 0xF);
    __asm__ volatile ("outb %0, $0xE9" :: "a"((uint8_t)'e'));
    isr_hex32(error);
    __asm__ volatile ("outb %0, $0xE9" :: "a"((uint8_t)'p'));
    isr_hex32(eip);
    __asm__ volatile ("outb %0, $0xE9" :: "a"((uint8_t)'c'));
    {
        uint32_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        isr_hex32(cr2);
    }
    __asm__ volatile ("outb %0, $0xE9" :: "a"((uint8_t)'s'));
    isr_hex32(user_esp);
    __asm__ volatile ("outb %0, $0xE9" :: "a"((uint8_t)'x'));
    isr_hex32(ecx);
    __asm__ volatile ("outb %0, $0xE9" :: "a"((uint8_t)'\n'));

    for (;;) {
        __asm__ volatile ("cli");
        __asm__ volatile ("hlt");
    }
}

void isr_init(void)
{
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);
}
