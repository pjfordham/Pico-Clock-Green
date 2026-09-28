#ifndef PIO_DISPLAY_H
#define PIO_DISPLAY_H

#include <stdint.h>

struct display_driver_t;

struct display_driver_t *display_init();
void display_print(struct display_driver_t *t);
uint32_t *display_get(struct display_driver_t *t);
void display_free(struct display_driver_t *t);

#endif
