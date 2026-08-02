#include <kernel/paging.h>
#include <kernel/pmm.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define PAGE_TOP 0x2000000

static uint32_t* kernel_pd;
static uint32_t* user_pt;

static uint32_t* pmm_alloc_zero_page(void)
{
    return (uint32_t*)pmm_alloc_zero();
}

void paging_init(void)
{
    kernel_pd = pmm_alloc_zero_page();
    user_pt = pmm_alloc_zero_page();

    for (uint32_t vaddr = 0; vaddr < PAGE_TOP; vaddr += 0x400000) {
        uint32_t pd_idx = vaddr >> 22;
        if (vaddr == USER_BASE) continue;

        uint32_t* pt = pmm_alloc_zero_page();
        for (int i = 0; i < 1024; i++)
            pt[i] = (vaddr + i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW;

        kernel_pd[pd_idx] = ((uint32_t)pt) | PAGE_PRESENT | PAGE_RW;
    }

    kernel_pd[USER_BASE >> 22] = ((uint32_t)user_pt) | PAGE_PRESENT | PAGE_RW | PAGE_USER;

    uint32_t lfb_idx = 0xFD000000 >> 22;
    uint32_t* lfb_pt = pmm_alloc_zero_page();
    for (int i = 0; i < 1024; i++)
        lfb_pt[i] = (0xFD000000 + i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW;
    kernel_pd[lfb_idx] = ((uint32_t)lfb_pt) | PAGE_PRESENT | PAGE_RW;

    __asm__ volatile ("mov %0, %%cr3" :: "r"(kernel_pd));
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0));
}

uint32_t paging_map_user_page(uint32_t vaddr, uint32_t phys, uint32_t flags)
{
    if (vaddr < USER_BASE || vaddr >= USER_SPACE_TOP) return 0;
    uint32_t idx = (vaddr >> 12) & 0x3FF;
    user_pt[idx] = (phys & 0xFFFFF000) | flags | PAGE_USER;
    __asm__ volatile ("invlpg (%0)" :: "r"(vaddr));
    return vaddr;
}

void paging_unmap_user_page(uint32_t vaddr)
{
    if (vaddr < USER_BASE || vaddr >= USER_SPACE_TOP) return;
    uint32_t idx = (vaddr >> 12) & 0x3FF;
    user_pt[idx] = 0;
    __asm__ volatile ("invlpg (%0)" :: "r"(vaddr));
}

uint32_t paging_alloc_user_pages(uint32_t vaddr, uint32_t pages)
{
    for (uint32_t i = 0; i < pages; i++) {
        uintptr_t phys = pmm_alloc_zero();
        if (!phys) return 0;
        if (!paging_map_user_page(vaddr + i * PAGE_SIZE, phys, PAGE_PRESENT | PAGE_RW))
            return 0;
    }
    return vaddr;
}
