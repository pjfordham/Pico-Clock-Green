#ifndef PIO_DISPLAY_H
#define PIO_DISPLAY_H

#include <stdint.h>

extern uint32_t display_buffer[8];
void display_init();
void display_print();

#endif
