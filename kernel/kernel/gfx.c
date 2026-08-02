#include <kernel/gfx.h>
#include <kernel/io.h>

#define VBE_DISPI_INDEX 0x01CE
#define VBE_DISPI_DATA  0x01CF
#define VBE_ENABLE      0x04

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define FONT_BYTES_PER_CHAR 16

static uint32_t* lfb;
static int gfx_w;
static int gfx_h;
static uint8_t font[256 * FONT_BYTES_PER_CHAR];

#define BACK_BUFFER_ADDR 0x00400000

static uint32_t* fb;
static uint32_t* back;

static int win_off_x;
static int win_off_y;
static int win_clip_x;
static int win_clip_y;
static int win_clip_w;
static int win_clip_h;
static int win_active;

static void vbe_index(uint16_t idx)
{
    outw(VBE_DISPI_INDEX, idx);
}

static void vbe_data(uint16_t val)
{
    outw(VBE_DISPI_DATA, val);
}

static uint32_t pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    uint32_t addr = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)func << 8) | (reg & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static uint32_t gfx_find_lfb(void)
{
    for (uint8_t dev = 0; dev < 32; dev++) {
        uint32_t id = pci_read_config(0, dev, 0, 0x00);
        if ((id & 0xFFFF) == 0xFFFF) continue;
        uint32_t cls = pci_read_config(0, dev, 0, 0x08);
        if (((cls >> 24) & 0xFF) == 0x03)
            return pci_read_config(0, dev, 0, 0x10) & 0xFFFFFFF0;
    }
    return 0;
}

static int font_glyph_ok(const uint8_t* f, char c)
{
    const uint8_t* g = f + (uint8_t)c * FONT_BYTES_PER_CHAR;
    if (c == 'A') {
        if (g[0] != 0x00 || g[1] != 0x00 || g[15] != 0x00) return 0;
        int fe = 0;
        int c6 = 0;
        for (int i = 2; i < 15; i++) {
            if (g[i] == 0xFE) fe++;
            if (g[i] == 0xC6) c6++;
        }
        if (fe != 1) return 0;
        if (c6 < 3) return 0;
        return 1;
    }
    return 1;
}

static void font_read_cfg(int mapv, int plane, int seqmode, uint8_t* out)
{
    uint8_t save_gc4 = inb(0x3CE);
    outb(0x3CE, 0x04); uint8_t save_gc5 = inb(0x3CF);
    outb(0x3CE, 0x05); uint8_t save_gc6 = inb(0x3CF);
    outb(0x3CE, 0x06); uint8_t save_gc7 = inb(0x3CF);
    uint8_t save_sr4 = inb(0x3C4);
    outb(0x3C4, 0x04); uint8_t save_sr5 = inb(0x3C5);

    outb(0x3C4, 0x04); outb(0x3C5, seqmode);
    outb(0x3CE, 0x04); outb(0x3CF, plane);
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);
    outb(0x3CE, 0x06); outb(0x3CF, mapv);

    for (int c = 0; c < 256; c++)
        for (int row = 0; row < FONT_BYTES_PER_CHAR; row++)
            out[c * FONT_BYTES_PER_CHAR + row] = *(const uint8_t*)(0xA0000 + c * 32 + row);

    outb(0x3C4, 0x04); outb(0x3C5, save_sr5);
    outb(0x3C4, save_sr4);
    outb(0x3CE, 0x04); outb(0x3CF, save_gc5);
    outb(0x3CE, 0x05); outb(0x3CF, save_gc6);
    outb(0x3CE, 0x06); outb(0x3CF, save_gc7);
    outb(0x3CE, save_gc4);
}

static void font_init(void)
{
    static const int planes[4] = {2, 3, 0, 1};
    static const int seqs[2] = {0x02, 0x00};
    for (int s = 0; s < 2; s++) {
        for (int i = 0; i < 4; i++) {
            font_read_cfg(0x00, planes[i], seqs[s], font);
            if (font_glyph_ok(font, 'A'))
                return;
        }
    }
    for (uint32_t i = 0; i < sizeof(font); i++)
        font[i] = 0;
}

void gfx_init(uint16_t width, uint16_t height)
{
    vbe_index(VBE_ENABLE);
    vbe_data(0x0000);

    font_init();

    vbe_index(0x01);
    vbe_data(width);
    vbe_index(0x02);
    vbe_data(height);
    vbe_index(0x03);
    vbe_data(32);
    vbe_index(0x07);
    vbe_data(width);
    vbe_index(0x04);
    vbe_data(0x0000);
    vbe_index(VBE_ENABLE);
    vbe_data(0x41);

    lfb = (uint32_t*)gfx_find_lfb();
    gfx_w = width;
    gfx_h = height;
    back = (uint32_t*)BACK_BUFFER_ADDR;
    fb = lfb;
}

void gfx_begin(void)
{
    fb = back;
}

void gfx_flush(void)
{
    for (int y = 0; y < gfx_h; y++) {
        uint32_t* src = back + y * gfx_w;
        uint32_t* dst = lfb + y * gfx_w;
        for (int x = 0; x < gfx_w; x++)
            dst[x] = src[x];
    }
    fb = lfb;
}

static void vga_text_mode_restore(void)
{
    outb(0x3C2, 0x67);

    static const uint8_t seq[5] = {0x03, 0x08, 0x03, 0x00, 0x03};
    for (int i = 0; i < 5; i++) {
        outb(0x3C4, i);
        outb(0x3C5, seq[i]);
    }

    outb(0x3D4, 0x11); outb(0x3D5, 0x0E);
    static const uint8_t crtc[25] = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF
    };
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc[i]);
    }

    static const uint8_t gc[9] = {0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x05, 0x0F, 0xFF};
    for (int i = 0; i < 9; i++) {
        outb(0x3CE, i);
        outb(0x3CF, gc[i]);
    }

    inb(0x3DA);
    static const uint8_t attr[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
    };
    for (int i = 0; i < 16; i++) {
        outb(0x3C0, i);
        outb(0x3C0, attr[i]);
    }
    outb(0x3C0, 0x10); outb(0x3C0, 0x0C);
    outb(0x3C0, 0x11); outb(0x3C0, 0x00);
    outb(0x3C0, 0x12); outb(0x3C0, 0x0F);
    outb(0x3C0, 0x13); outb(0x3C0, 0x08);
    outb(0x3C0, 0x14); outb(0x3C0, 0x00);
    outb(0x3C0, 0x20);

    static const uint8_t dac[16][3] = {
        {0, 0, 0}, {0, 0, 170}, {0, 170, 0}, {0, 170, 170},
        {170, 0, 0}, {170, 0, 170}, {170, 85, 0}, {170, 170, 170},
        {85, 85, 85}, {85, 85, 255}, {85, 255, 85}, {85, 255, 255},
        {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255}
    };
    outb(0x3C8, 0);
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 3; j++)
            outb(0x3C9, dac[i][j]);
}

void gfx_disable(void)
{
    vbe_index(VBE_ENABLE);
    vbe_data(0x0000);
    vga_text_mode_restore();
    lfb = 0;
    gfx_w = 0;
    gfx_h = 0;
}

int gfx_width(void)
{
    return gfx_w;
}

int gfx_height(void)
{
    return gfx_h;
}

void gfx_set_window(int x, int y, int w, int h)
{
    win_off_x = x;
    win_off_y = y;
    win_clip_x = x;
    win_clip_y = y;
    win_clip_w = w;
    win_clip_h = h;
    win_active = 1;
}

void gfx_clear_window(void)
{
    win_active = 0;
}

int gfx_window_w(void)
{
    return win_active ? win_clip_w : gfx_w;
}

int gfx_window_h(void)
{
    return win_active ? win_clip_h : gfx_h;
}

void gfx_clear(uint32_t color)
{
    if (win_active) {
        gfx_fill_rect(0, 0, win_clip_w, win_clip_h, color);
        return;
    }
    for (int y = 0; y < gfx_h; y++)
        for (int x = 0; x < gfx_w; x++)
            fb[y * gfx_w + x] = color;
}

void gfx_putpixel(int x, int y, uint32_t color)
{
    if (win_active) {
        x += win_off_x;
        y += win_off_y;
        if (x < win_clip_x || y < win_clip_y) return;
        if (x >= win_clip_x + win_clip_w || y >= win_clip_y + win_clip_h) return;
    }
    if (x < 0 || y < 0 || x >= gfx_w || y >= gfx_h) return;
    fb[y * gfx_w + x] = color;
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (win_active) {
        x += win_off_x;
        y += win_off_y;
        int wx0 = win_clip_x;
        int wy0 = win_clip_y;
        int wx1 = win_clip_x + win_clip_w;
        int wy1 = win_clip_y + win_clip_h;
        if (x < wx0) { w -= wx0 - x; x = wx0; }
        if (y < wy0) { h -= wy0 - y; y = wy0; }
        if (x + w > wx1) w = wx1 - x;
        if (y + h > wy1) h = wy1 - y;
        if (w <= 0 || h <= 0) return;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= gfx_w || y >= gfx_h) return;
    if (w > gfx_w - x) w = gfx_w - x;
    if (h > gfx_h - y) h = gfx_h - y;
    for (int yy = 0; yy < h; yy++) {
        uint32_t* row = fb + (y + yy) * gfx_w + x;
        for (int xx = 0; xx < w; xx++)
            row[xx] = color;
    }
}

void gfx_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    gfx_fill_rect(x, y, w, 1, color);
    gfx_fill_rect(x, y + h - 1, w, 1, color);
    gfx_fill_rect(x, y, 1, h, color);
    gfx_fill_rect(x + w - 1, y, 1, h, color);
}

void gfx_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg)
{
    const uint8_t* g = font + (uint8_t)c * FONT_BYTES_PER_CHAR;
    int transparent = (bg == GFX_TRANSPARENT);
    for (int row = 0; row < FONT_BYTES_PER_CHAR; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 8; col++) {
            if ((bits >> (7 - col)) & 1) {
                gfx_putpixel(x + col, y + row, fg);
            } else if (!transparent) {
                gfx_putpixel(x + col, y + row, bg);
            }
        }
    }
}

void gfx_draw_text(int x, int y, const char* s, uint32_t fg, uint32_t bg)
{
    int cx = x;
    while (*s) {
        if (*s == '\n') {
            cx = x;
            y += FONT_BYTES_PER_CHAR;
        } else {
            gfx_draw_char(cx, y, *s, fg, bg);
            cx += 8;
        }
        s++;
    }
}

static const uint16_t gfx_cursor_arrow[16] = {
    0x0001, 0x0003, 0x0007, 0x000F,
    0x001F, 0x003F, 0x007F, 0x00FF,
    0x01FF, 0x03FF, 0x07FF, 0x0E07,
    0x1C07, 0x3807, 0x7007, 0xE007,
};

static int cur_last_x = -1;
static int cur_last_y = -1;
static uint32_t cur_saved[16 * 16];

void gfx_cursor_reset(void)
{
    cur_last_x = -1;
    cur_last_y = -1;
}

void gfx_draw_cursor_abs(int x, int y)
{
    if (cur_last_x >= 0) {
        for (int r = 0; r < 16; r++)
            for (int c = 0; c < 16; c++) {
                int px = cur_last_x + c;
                int py = cur_last_y + r;
                if (px >= 0 && py >= 0 && px < gfx_w && py < gfx_h)
                    fb[py * gfx_w + px] = cur_saved[r * 16 + c];
            }
    }

    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++) {
            int px = x + c;
            int py = y + r;
            if (px >= 0 && py >= 0 && px < gfx_w && py < gfx_h)
                cur_saved[r * 16 + c] = fb[py * gfx_w + px];
        }

    for (int r = 0; r < 16; r++) {
        uint16_t bits = gfx_cursor_arrow[r];
        for (int c = 0; c < 16; c++) {
            if (bits & (0x8000 >> c)) {
                int px = x + c;
                int py = y + r;
                if (px >= 0 && py >= 0 && px < gfx_w && py < gfx_h)
                    fb[py * gfx_w + px] = 0xFFFFFF;
            }
        }
    }

    cur_last_x = x;
    cur_last_y = y;
}
