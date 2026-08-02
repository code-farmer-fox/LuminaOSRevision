#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2
#define PAGE_USER    0x4

#define USER_BASE    0x01000000
#define USER_SPACE_TOP (USER_BASE + 0x00400000)

void paging_init(void);
uint32_t paging_map_user_page(uint32_t vaddr, uint32_t phys, uint32_t flags);
void paging_unmap_user_page(uint32_t vaddr);
uint32_t paging_alloc_user_pages(uint32_t vaddr, uint32_t pages);

#endif
