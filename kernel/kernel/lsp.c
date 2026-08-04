#include <kernel/lsp.h>
#include <kernel/paging.h>
#include <kernel/fat16.h>
#include <kernel/gdt.h>
#include <kernel/system.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define USER_STACK_TOP (USER_SPACE_TOP)
#define USER_STACK_PAGES 16

extern void enter_user_mode(uint32_t eip, uint32_t esp, uint32_t cs, uint32_t ss, uint32_t eflags);

static void lsp_zero(uint32_t addr, uint32_t size)
{
    uint8_t* p = (uint8_t*)addr;
    for (uint32_t i = 0; i < size; i++)
        p[i] = 0;
}

int lsp_load(const char* path)
{
    lsp_header_t hdr;
    uint32_t hdr_sz = 0;

    if (fat16_read_file(path, &hdr, sizeof(hdr), &hdr_sz) != 0)
        return -1;
    if (hdr_sz < sizeof(hdr))
        return -1;

    if (hdr.magic[0] != LSP_MAGIC0 || hdr.magic[1] != LSP_MAGIC1 || hdr.magic[2] != LSP_MAGIC2)
        return -1;
    if (hdr.version != LSP_VERSION)
        return -1;

    uint32_t total_pages = (hdr.load_size + hdr.bss_size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (total_pages == 0) return -1;

    if (!paging_alloc_user_pages(USER_BASE, total_pages))
        return -1;

    uint32_t got = 0;
    if (fat16_read_file(path, (void*)USER_BASE, hdr.load_size, &got) != 0)
        return -1;

    uint32_t entry = USER_BASE + hdr.entry_offset;

    uint32_t stack_vaddr = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
    if (!paging_alloc_user_pages(stack_vaddr, USER_STACK_PAGES))
        return -1;

    lsp_zero(USER_BASE + hdr.load_size, hdr.bss_size);

    enter_user_mode(entry, USER_STACK_TOP - 0x1000, GDT_USER_CS | 3, GDT_USER_DS | 3, 0x202);
    return 0;
}
