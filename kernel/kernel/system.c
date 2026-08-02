#include <kernel/system.h>
#include <kernel/io.h>

void system_halt(void)
{
    __asm__ volatile ("cli; hlt");
}

void system_reboot(void)
{
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
    system_halt();
}
