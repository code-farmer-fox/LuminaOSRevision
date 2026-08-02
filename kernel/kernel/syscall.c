#include <kernel/syscall.h>
#include <kernel/gfx.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/fat16.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>
#include <kernel/heap.h>
#include <kernel/desktop.h>
#include <stdint.h>

extern void syscall_asm(void);
extern void kernel_resume(void);

static int sc_exit(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    kernel_resume();
    return 0;
}

static int sc_putpixel(int x, int y, int color, int d)
{
    (void)d;
    gfx_begin();
    gfx_putpixel(x, y, (uint32_t)color);
    return 0;
}

static int sc_fill_rect(int x, int y, int w, int color)
{
    int h = (w >> 16) & 0xFFFF;
    w &= 0xFFFF;
    gfx_begin();
    gfx_fill_rect(x, y, w, h, (uint32_t)color);
    return 0;
}

static int sc_draw_rect(int x, int y, int w, int color)
{
    int h = (w >> 16) & 0xFFFF;
    w &= 0xFFFF;
    gfx_begin();
    gfx_draw_rect(x, y, w, h, (uint32_t)color);
    return 0;
}

static int sc_draw_char(int x, int y, int color, int d)
{
    gfx_begin();
    gfx_draw_char(x, y, (char)d, (uint32_t)color, GFX_TRANSPARENT);
    return 0;
}

static int sc_draw_text(int x, int y, int p, int d)
{
    gfx_begin();
    gfx_draw_text(x, y, (const char*)p, (uint32_t)d, GFX_TRANSPARENT);
    return 0;
}

static int sc_gfx_clear(int color, int b, int c, int d)
{
    (void)b; (void)c; (void)d;
    gfx_begin();
    gfx_clear((uint32_t)color);
    return 0;
}

static int sc_gfx_flush(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    gfx_flush();
    return 0;
}

static int sc_gfx_w(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    return gfx_width();
}

static int sc_gfx_h(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    return gfx_height();
}

static int sc_getchar(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    return keyboard_getchar();
}

static int sc_getchar_nb(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    return keyboard_getchar_nonblock();
}

static int sc_mouse_x(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    return mouse_x();
}
static int sc_mouse_y(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    return mouse_y();
}

static int sc_mouse_l(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    return mouse_left();
}

static int sc_mouse_r(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    return mouse_right();
}

static int sc_mouse_click(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    if (!mouse_click_peek())
        return 0;
    lsp_win_info_t wi;
    desktop_win_info(&wi);
    int mx = mouse_x();
    int my = mouse_y();
    if (mx >= wi.close_x && mx < wi.close_x + wi.close_w &&
        my >= wi.close_y && my < wi.close_y + wi.close_h)
        return 0;
    return mouse_click_consume();
}static int sc_file_read(int a, int b, int c, int d)
{
    (void)d;
    uint32_t sz;
    uint8_t buf[4096];
    if (fat16_read_file((const char*)a, buf, sizeof(buf), &sz) != 0)
        return -1;
    if (sz > (uint32_t)c) sz = c;
    for (uint32_t i = 0; i < sz; i++)
        ((uint8_t*)b)[i] = buf[i];
    return (int)sz;
}

static int sc_file_write(int a, int b, int c, int d)
{
    (void)d;
    return fat16_write_file((const char*)a, (const uint8_t*)b, (uint32_t)c);
}

static int sc_file_list(int a, int b, int c, int d)
{
    (void)b; (void)d;
    fat16_entry_t entries[64];
    int count = 0;
    if (fat16_list_dir(entries, 64, &count) != 0) return -1;
    lsp_dir_entry_t* out = (lsp_dir_entry_t*)a;
    int max = c / (int)sizeof(lsp_dir_entry_t);
    int n = 0;
    for (int i = 0; i < count && n < max; i++) {
        int len = 0;
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++)
            out[n].name[len++] = entries[i].name[j];
        if (entries[i].name[8] != ' ') {
            out[n].name[len++] = '.';
            for (int j = 8; j < 11 && entries[i].name[j] != ' '; j++)
                out[n].name[len++] = entries[i].name[j];
        }
        while (len < 12) out[n].name[len++] = ' ';
        out[n].attr = entries[i].attr;
        out[n].pad = 0;
        out[n].size = entries[i].size;
        n++;
    }
    return n;
}

static int sc_win_info(int a, int b, int c, int d)
{
    (void)b; (void)c; (void)d;
    lsp_win_info_t* info = (lsp_win_info_t*)a;
    if (!info) return -1;
    desktop_win_info(info);
    return 0;
}

static int sc_sysinfo(int a, int b, int c, int d)
{
    (void)b; (void)c; (void)d;
    lsp_sysinfo_t* info = (lsp_sysinfo_t*)a;
    if (!info) return -1;
    info->mem_total_kb = PMM_MAX_PHYS / 1024;
    info->mem_free_kb = (pmm_free_page_count() * PMM_PAGE_SIZE) / 1024;
    info->mem_used_kb = (pmm_used_page_count() * PMM_PAGE_SIZE) / 1024;
    info->heap_free_kb = heap_free_bytes() / 1024;
    info->heap_used_kb = heap_used_bytes() / 1024;
    return 0;
}

static int sc_win_should_close(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    if (keyboard_getchar_nonblock() == 27)
        return 1;
    if (mouse_click_peek()) {
        lsp_win_info_t wi;
        desktop_win_info(&wi);
        int mx = mouse_x();
        int my = mouse_y();
        if (mx >= wi.close_x && mx < wi.close_x + wi.close_w &&
            my >= wi.close_y && my < wi.close_y + wi.close_h) {
            mouse_click_consume();
            return 1;
        }
    }
    return 0;
}

static int sc_sleep(int ms, int b, int c, int d)
{
    (void)b; (void)c; (void)d;
    volatile int i;
    for (i = 0; i < ms * 20000; i++) { __asm__ volatile (""); }
    return 0;
}

static char g_open_file[64];
static int g_has_open_file = 0;

static int sc_open_file(int a, int b, int c, int d)
{
    (void)b; (void)c; (void)d;
    const char* src = (const char*)a;
    int i = 0;
    while (src[i] && i < (int)sizeof(g_open_file) - 1) {
        g_open_file[i] = src[i];
        i++;
    }
    g_open_file[i] = '\0';
    g_has_open_file = 1;
    return 0;
}

static int sc_get_open_file(int a, int b, int c, int d)
{
    (void)b; (void)d;
    if (!g_has_open_file) return 0;
    char* dst = (char*)a;
    int i = 0;
    while (g_open_file[i] && i < c - 1) {
        dst[i] = g_open_file[i];
        i++;
    }
    dst[i] = '\0';
    g_has_open_file = 0;
    return i;
}

int syscall_pending_file(char* out, int max)
{
    if (!g_has_open_file) return 0;
    int i = 0;
    while (g_open_file[i] && i < max - 1) {
        out[i] = g_open_file[i];
        i++;
    }
    out[i] = '\0';
    return i;
}

static int sc_cursor(int a, int b, int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    gfx_begin();
    gfx_draw_cursor_abs(mouse_x(), mouse_y());
    gfx_flush();
    return 0;
}

typedef int (*sc_fn_t)(int, int, int, int);

static const sc_fn_t syscall_table[SYSCALL_COUNT] = {
    [SYSCALL_EXIT]       = sc_exit,
    [SYSCALL_PUTPIXEL]   = sc_putpixel,
    [SYSCALL_FILL_RECT]  = sc_fill_rect,
    [SYSCALL_DRAW_RECT]  = sc_draw_rect,
    [SYSCALL_DRAW_CHAR]  = sc_draw_char,
    [SYSCALL_DRAW_TEXT]  = sc_draw_text,
    [SYSCALL_GFX_CLEAR]  = sc_gfx_clear,
    [SYSCALL_GFX_FLUSH]  = sc_gfx_flush,
    [SYSCALL_GFX_W]      = sc_gfx_w,
    [SYSCALL_GFX_H]      = sc_gfx_h,
    [SYSCALL_GETCHAR]    = sc_getchar,
    [SYSCALL_GETCHAR_NB] = sc_getchar_nb,
    [SYSCALL_MOUSE_X]    = sc_mouse_x,
    [SYSCALL_MOUSE_Y]    = sc_mouse_y,
    [SYSCALL_MOUSE_L]    = sc_mouse_l,
    [SYSCALL_MOUSE_R]    = sc_mouse_r,
    [SYSCALL_MOUSE_CLICK]= sc_mouse_click,
    [SYSCALL_FILE_READ]  = sc_file_read,
    [SYSCALL_FILE_WRITE] = sc_file_write,
    [SYSCALL_FILE_LIST]  = sc_file_list,
    [SYSCALL_SLEEP]      = sc_sleep,
    [SYSCALL_CURSOR]     = sc_cursor,
    [SYSCALL_WIN_INFO]   = sc_win_info,
    [SYSCALL_SYSINFO]    = sc_sysinfo,
    [SYSCALL_WIN_SHOULD_CLOSE] = sc_win_should_close,
    [SYSCALL_OPEN_FILE] = sc_open_file,
    [SYSCALL_GET_OPEN_FILE] = sc_get_open_file,
};

void syscall_init(void)
{
    idt_set_gate(0x80, (uint32_t)syscall_asm, 0x08, 0xEF);
}

int syscall_dispatch(int num, int a1, int a2, int a3, int a4)
{
    if (num < 0 || num >= SYSCALL_COUNT || !syscall_table[num])
        return -1;
    return syscall_table[num](a1, a2, a3, a4);
}
