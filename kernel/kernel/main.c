#include <kernel/tty.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/isr.h>
#include <kernel/keyboard.h>
#include <kernel/system.h>
#include <kernel/fat16.h>
#include <kernel/pmm.h>
#include <kernel/paging.h>
#include <kernel/heap.h>
#include <kernel/syscall.h>
#include <kernel/gfx.h>
#include <kernel/mouse.h>
#include <kernel/timer.h>
#include <kernel/sched.h>
#include <stdint.h>

/* Provided by shell.c */
extern void shell_set_fs_ready(int ready);
extern void _start(void);

/* syscall stack page, used by tss_set_stack */
static uint32_t syscall_stack;

static void print_uint(uint32_t value)
{
    char buf[10];
    int len = 0;
    if (value == 0) {
        tty_putchar('0');
        return;
    }
    while (value > 0) {
        buf[len++] = '0' + value % 10;
        value /= 10;
    }
    while (len > 0)
        tty_putchar(buf[--len]);
}

static void print_banner(void)
{
    tty_puts("============================================\n");
    tty_puts("         LuminaOS v0.7.1 - 32-bit\n");
    tty_puts("============================================\n\n");
}

static void fs_bringup(void)
{
    tty_puts("Initializing ATA...\n");

    if (fat16_init() != 0) {
        tty_puts("FAT16 init failed (disk I/O only)\n\n");
        shell_set_fs_ready(0);
        return;
    }

    tty_puts("FAT16 filesystem ready\n\n");
    shell_set_fs_ready(1);

    static uint8_t font_buf[8192];
    uint32_t fsz = 0;
    if (fat16_read_file("FONT16.BIN", font_buf, sizeof(font_buf), &fsz) == 0) {
        gfx_cjk_load(font_buf, fsz);
        tty_puts("CJK font loaded (");
        print_uint(gfx_cjk_count());
        tty_puts(" glyphs)\n");
    } else {
        tty_puts("CJK font not found\n");
    }
}

void kernel_main(uint8_t boot_drive)
{
    (void)boot_drive;

    /* --- core CPU / interrupt infrastructure --- */
    tty_init();
    gdt_init();
    idt_init();
    isr_init();
    irq_init();
    keyboard_init();

    /* --- memory management --- */
    pmm_init();
    heap_init();
    paging_init();

    /* --- syscall entry (ring3 -> ring0) --- */
    syscall_stack = pmm_alloc_zero();
    tss_set_stack(syscall_stack + 4096);
    syscall_init();

    /* --- timer + scheduler --- */
    timer_init();
    sched_init();

    print_banner();

    /* --- filesystem + fonts --- */
    fs_bringup();

    /* --- hand off to interactive shell (never returns) --- */
    _start();
}