#ifndef TTY_H
#define TTY_H

#include <stddef.h>
#include <stdint.h>

void tty_init(void);
void tty_clear(void);
void tty_setcolor(uint8_t color);
void tty_putchar(char c);
void tty_write(const char* data, size_t size);
void tty_puts(const char* str);

#endif
