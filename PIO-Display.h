#ifndef PIO_DISPLAY_H
#define PIO_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/time.h"

extern uint32_t display_buffer[8];
int display_init();
void display_print();

#endif
