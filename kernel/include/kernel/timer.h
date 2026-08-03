#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define TIMER_HZ 1000

void timer_init(void);
uint32_t timer_ticks(void);
void timer_wait(uint32_t ms);

#endif
