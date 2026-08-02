#include <kernel/mouse.h>
#include <kernel/irq.h>
#include <kernel/io.h>
#define PS2_CMD  0x64
#define PS2_DATA 0x60

static volatile int mouse_avail;
static volatile int mouse_pos_x;
static volatile int mouse_pos_y;
static volatile int mouse_btn_left;
static volatile int mouse_btn_right;
static volatile int mouse_click_left;

static void mouse_wait_write(void)
{
    for (int i = 0; i < 100000; i++) {
        if (!(inb(PS2_CMD) & 0x02)) return;
    }
}

static void mouse_wait_read(void)
{
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_CMD) & 0x01) return;
    }
}
static void mouse_cmd(uint8_t cmd)
{
    mouse_wait_write();
    outb(PS2_CMD, 0xD4);
    mouse_wait_write();
    outb(PS2_DATA, cmd);
    mouse_wait_read();
    inb(PS2_DATA);
}

static void mouse_callback(void)
{
    uint8_t data = inb(PS2_DATA);
    static uint8_t packet[3];
    static int cycle = 0;

    packet[cycle] = data;
    cycle++;
    if (cycle == 3) {
        cycle = 0;
        if (!(packet[0] & 0x08)) return;

        int dx = (int)(int8_t)packet[1];
        int dy = (int)(int8_t)packet[2];

        mouse_pos_x += dx;
        mouse_pos_y -= dy;
        if (mouse_pos_x < 0) mouse_pos_x = 0;
        if (mouse_pos_y < 0) mouse_pos_y = 0;
        if (mouse_pos_x > 799) mouse_pos_x = 799;
        if (mouse_pos_y > 599) mouse_pos_y = 599;

        mouse_btn_left = packet[0] & 0x01;
        mouse_btn_right = packet[0] & 0x02;
        if (mouse_btn_left) mouse_click_left = 1;
    }
}

void mouse_init(void)
{
    mouse_wait_write();
    outb(PS2_CMD, 0xA8);
    mouse_wait_read();
    inb(PS2_DATA);

    mouse_wait_write();
    outb(PS2_CMD, 0x20);
    mouse_wait_read();
    uint8_t status = inb(PS2_DATA);
    status |= 0x02;
    mouse_wait_write();
    outb(PS2_CMD, 0x60);
    mouse_wait_write();
    outb(PS2_DATA, status);

    mouse_cmd(0xF6);
    mouse_cmd(0xF4);

    mouse_pos_x = 400;
    mouse_pos_y = 300;
    mouse_avail = 1;

    irq_install_handler(12, mouse_callback);
}

int mouse_available(void)
{
    return mouse_avail;
}

int mouse_x(void)
{
    return mouse_pos_x;
}

int mouse_y(void)
{
    return mouse_pos_y;
}

int mouse_left(void)
{
    return mouse_btn_left;
}

int mouse_right(void)
{
    return mouse_btn_right;
}

int mouse_click_peek(void)
{
    return mouse_click_left;
}

int mouse_click_consume(void)
{
    if (mouse_click_left) {
        mouse_click_left = 0;
        return 1;
    }
    return 0;
}
