#ifndef __TICKS_H__
#define __TICKS_H__

#include <stdint.h>

void ticks_init();
volatile uint32_t ticks();
volatile uint32_t seconds();

#endif
