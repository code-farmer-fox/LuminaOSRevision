#include <kernel/desktop.h>
#include <kernel/gfx.h>
#include <kernel/mouse.h>
#include <kernel/system.h>
#include <kernel/lsp.h>
#include <kernel/syscall.h>
#include <stdint.h>

#define FONT_W 8
#define FONT_H 16

#define DESK_W 800
#define DESK_H 600
#define TASKBAR_H 40

#define WIN_X 100
#define WIN_Y 80
#define WIN_W 600
#define WIN_H 440
#define TITLE_H 30

typedef struct {
    const char* name;
    uint32_t color;
    const char* lsp;
    int action;
} desktop_icon_t;

enum {
    APP_NONE = 0,
    APP_LAUNCH,
    APP_REBOOT,
    APP_HALT
};

static const desktop_icon_t icons[] = {
    { "文件",   0x4F7FBF, "FILES.LSP",   APP_LAUNCH },
    { "记事",   0x3FAF6F, "NOTEPAD.LSP", APP_LAUNCH },
    { "关于",   0xBF6F3F, "ABOUT.LSP",   APP_LAUNCH },
    { "系统",   0x9F9FAF, "SYSTEM.LSP",  APP_LAUNCH },
    { "重启",   0xAF4F4F, 0,             APP_REBOOT },
    { "关机",   0x8F8FAF, 0,             APP_HALT  },
    {"命令行",  0x000000, "CMD.LSP",     APP_LAUNCH}
};
#define ICON_COUNT ((int)(sizeof(icons) / sizeof(icons[0])))

#define ICON_W 64
#define ICON_H 64
#define ICON_LABEL_H 24
#define ICON_COLS 3
#define ICON_CELL_X 100
#define ICON_CELL_Y 104

static int icon_x(int i)
{
    return 40 + (i % ICON_COLS) * ICON_CELL_X;
}

static int icon_y(int i)
{
    return 30 + (i / ICON_COLS) * ICON_CELL_Y;
}

static int dt_str_len(const char* s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void dt_draw_cursor(int x, int y)
{
    gfx_draw_cursor_abs(x, y);
}

static void dt_draw_wallpaper(void)
{
    uint32_t top = 0x1E3A5F;
    uint32_t bot = 0x0F2238;
    int h = DESK_H - TASKBAR_H;
    for (int y = 0; y < h; y++) {
        int t = (y * 255) / (h - 1);
        int r = ((top >> 16) & 0xFF) + ((((bot >> 16) & 0xFF) - ((top >> 16) & 0xFF)) * t / 255);
        int g = ((top >> 8) & 0xFF) + ((((bot >> 8) & 0xFF) - ((top >> 8) & 0xFF)) * t / 255);
        int b = (top & 0xFF) + (((bot & 0xFF) - (top & 0xFF)) * t / 255);
        gfx_fill_rect(0, y, DESK_W, 1, (r << 16) | (g << 8) | b);
    }
}

static void dt_draw_icon(int idx, int hover)
{
    const desktop_icon_t* ic = &icons[idx];
    int x = icon_x(idx);
    int y = icon_y(idx);

    uint32_t base = ic->color;
    uint32_t light = 0xFFFFFF;
    uint32_t dark = 0x000000;

    gfx_fill_rect(x + 3, y + 3, ICON_W, ICON_H, 0x00000055);
    gfx_fill_rect(x, y, ICON_W, ICON_H, base);
    gfx_fill_rect(x, y, ICON_W, 3, light);
    gfx_fill_rect(x, y, 3, ICON_H, light);

    gfx_fill_rect(x + 6, y + 6, ICON_W - 12, 10, dark);
    gfx_fill_rect(x + 6, y + 6, ICON_W - 12, 10, 0x00000088);
    gfx_draw_text(x + 8, y + 8, "Lumina", 0xFFFFFF, base);

    if (hover)
        gfx_draw_rect(x, y, ICON_W, ICON_H, 0xFFFFFF);
    else
        gfx_draw_rect(x, y, ICON_W, ICON_H, dark);

    int text_w = gfx_text_width(ic->name);
    int text_x = x + (ICON_W - text_w) / 2;
    if (text_x < x) text_x = x;
    gfx_draw_text(text_x, y + ICON_H + 8, ic->name, 0xFFFFFF, 0x0F2238);
}

static void dt_draw_taskbar(const char* user)
{
    gfx_fill_rect(0, DESK_H - TASKBAR_H, DESK_W, TASKBAR_H, 0x202020);
    gfx_fill_rect(0, DESK_H - TASKBAR_H, DESK_W, 2, 0x4F7FBF);

    gfx_fill_rect(8, DESK_H - TASKBAR_H + 6, 120, TASKBAR_H - 12, 0x303030);
    gfx_draw_text(28, DESK_H - TASKBAR_H + 12, "LuminaOS", 0xE0E0E0, 0x303030);

    char ubuf[48];
    int n = 0;
    const char* pre = "User: ";
    while (*pre) ubuf[n++] = *pre++;
    if (user && user[0]) {
        while (*user && n < 40) ubuf[n++] = *user++;
    } else {
        ubuf[n++] = 'g'; ubuf[n++] = 'u'; ubuf[n++] = 'e'; ubuf[n++] = 's'; ubuf[n++] = 't';
    }
    ubuf[n] = '\0';

    int tw = dt_str_len(ubuf) * FONT_W;
    gfx_draw_text(DESK_W - tw - 10, DESK_H - TASKBAR_H + 12, ubuf, 0xE0E0E0, 0x202020);
}

static void dt_draw_window(const char* title)
{
    gfx_fill_rect(WIN_X, WIN_Y, WIN_W, WIN_H, 0xF7F7F7);
    gfx_draw_rect(WIN_X, WIN_Y, WIN_W, WIN_H, 0x404040);

    uint32_t c0 = 0x4040A0;
    uint32_t c1 = 0x28287A;
    for (int yy = 0; yy < TITLE_H; yy++) {
        int t = (yy * 255) / (TITLE_H - 1);
        int r = ((c0 >> 16) & 0xFF) + ((((c1 >> 16) & 0xFF) - ((c0 >> 16) & 0xFF)) * t / 255);
        int g = ((c0 >> 8) & 0xFF) + ((((c1 >> 8) & 0xFF) - ((c0 >> 8) & 0xFF)) * t / 255);
        int b = (c0 & 0xFF) + (((c1 & 0xFF) - (c0 & 0xFF)) * t / 255);
        gfx_fill_rect(WIN_X + 1, WIN_Y + yy, WIN_W - 2, 1, (r << 16) | (g << 8) | b);
    }
    gfx_draw_text(WIN_X + 8, WIN_Y + (TITLE_H - FONT_H) / 2, title, 0xFFFFFF, 0x34348C);

    gfx_fill_rect(WIN_X + WIN_W - 26, WIN_Y + 5, 20, 18, 0xC04040);
    gfx_fill_rect(WIN_X + WIN_W - 26, WIN_Y + 5, 20, 2, 0xE06060);
    gfx_draw_text(WIN_X + WIN_W - 22, WIN_Y + 6, "X", 0xFFFFFF, 0xC04040);
}

static int dt_point_in(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

void desktop_win_info(lsp_win_info_t* info)
{
    info->win_x = WIN_X;
    info->win_y = WIN_Y;
    info->win_w = WIN_W;
    info->win_h = WIN_H;
    info->client_x = WIN_X + 1;
    info->client_y = WIN_Y + TITLE_H;
    info->client_w = WIN_W - 2;
    info->client_h = WIN_H - TITLE_H - 1;
    info->close_x = WIN_X + WIN_W - 26;
    info->close_y = WIN_Y + 5;
    info->close_w = 20;
    info->close_h = 18;
}

static void dt_prepare_win(const char* title, const char* user)
{
    gfx_begin();
    dt_draw_wallpaper();
    for (int i = 0; i < ICON_COUNT; i++)
        dt_draw_icon(i, 0);
    dt_draw_taskbar(user);
    dt_draw_window(title);
    dt_draw_cursor(mouse_x(), mouse_y());

    int cx = WIN_X + 1;
    int cy = WIN_Y + TITLE_H;
    int cw = WIN_W - 2;
    int ch = WIN_H - TITLE_H - 1;
    gfx_set_window(cx, cy, cw, ch);
    gfx_fill_rect(0, 0, cw, ch, 0x202020);
    gfx_flush();
}

static void dt_run_app(const char* title, const char* path, const char* user)
{
    (void)user;
    dt_prepare_win(title, user);

    int r = lsp_load(path);
    gfx_clear_window();
    gfx_cursor_reset();
    (void)r;

    char open_file[64];
    if (syscall_pending_file(open_file, sizeof(open_file)) > 0) {
        dt_prepare_win("记事本", user);
        r = lsp_load("NOTEPAD.LSP");
        gfx_clear_window();
        gfx_cursor_reset();
        (void)r;
    }
}

int desktop_run(int fs_ready, const char* user)
{
    (void)fs_ready;

    int prev_x = -1;
    int prev_y = -1;
    int dirty = 1;

    while (1) {
        int mx = mouse_x();
        int my = mouse_y();
        if (mx != prev_x || my != prev_y) dirty = 1;
        prev_x = mx;
        prev_y = my;

        int left = mouse_click_consume();
        if (left) {
            for (int i = 0; i < ICON_COUNT; i++) {
                if (dt_point_in(mx, my, icon_x(i), icon_y(i), ICON_W, ICON_H + ICON_LABEL_H)) {
                    if (icons[i].action == APP_REBOOT) {
                        gfx_disable();
                        system_reboot();
                    } else if (icons[i].action == APP_HALT) {
                        system_halt();
                    } else if (icons[i].action == APP_LAUNCH) {
                        dt_run_app(icons[i].name, icons[i].lsp, user);
                        gfx_cursor_reset();
                    }
                    dirty = 1;
                    break;
                }
            }
        }

        if (dirty) {
            gfx_begin();
            dt_draw_wallpaper();

            for (int i = 0; i < ICON_COUNT; i++) {
                int hover = dt_point_in(mx, my, icon_x(i), icon_y(i), ICON_W, ICON_H + ICON_LABEL_H);
                dt_draw_icon(i, hover);
            }

            dt_draw_taskbar(user);
            dt_draw_cursor(mx, my);
            gfx_flush();
            dirty = 0;
        }
    }
}
