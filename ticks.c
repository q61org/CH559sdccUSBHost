#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "CH559.h"
#include "compiler.h"

#if 0
static volatile uint32_t g_ticks;


void ticks_init()
{
    g_ticks = 0;
    TH0 = 0x85;
    TL0 = 0xee;
    TMOD = 1;
    TR0 = 1;
}

INTERRUPT(timer0_isr, INT_NO_TMR0)
{ // 128 counts per second
    TH0 = 0x85;
    TL0 = 0xee;
    ++g_ticks;
}

volatile uint32_t ticks()
{
    return g_ticks;
}

volatile uint32_t seconds()
{
    return (g_ticks >> 7);
}

#endif
