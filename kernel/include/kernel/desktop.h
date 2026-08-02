#ifndef DESKTOP_H
#define DESKTOP_H

#include <kernel/syscall.h>

int desktop_run(int fs_ready, const char* user);
void desktop_win_info(lsp_win_info_t* info);

#endif
