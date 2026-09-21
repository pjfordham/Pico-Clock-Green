#ifndef _NTP_H
#define _NTP_H

#include "pico/critical_section.h"
#include <time.h>

typedef struct NTP_T NTP_T;

NTP_T* ntp_init(void);
void   ntp_run(NTP_T *state);
void   ntp_finish(NTP_T *state);
struct tm ntp_get_utc(void);

#endif
