#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

void mouse_init(void);
int mouse_available(void);
int mouse_x(void);
int mouse_y(void);
int mouse_left(void);
int mouse_right(void);
int mouse_click_consume(void);
int mouse_click_peek(void);

#endif
