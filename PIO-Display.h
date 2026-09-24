#ifndef PIO_DISPLAY_H
#define PIO_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/time.h"

extern uint32_t display_buffer[8];
int display_init();
bool repeating_timer_callback_ms(struct repeating_timer *t);
void display_print();

#endif
