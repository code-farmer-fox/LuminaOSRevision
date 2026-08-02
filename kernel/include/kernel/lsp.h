#ifndef LSP_H
#define LSP_H

#include <stdint.h>

#define LSP_MAGIC0 'L'
#define LSP_MAGIC1 'S'
#define LSP_MAGIC2 'P'
#define LSP_VERSION 1

typedef struct {
    char magic[3];
    uint8_t version;
    uint32_t entry_offset;
    uint32_t load_size;
    uint32_t bss_size;
    uint32_t reserved;
} __attribute__((packed)) lsp_header_t;

#define LSP_HEADER_SIZE ((int)sizeof(lsp_header_t))

int lsp_load(const char* path);

#endif
