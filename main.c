typedef unsigned char *PUINT8;
typedef unsigned char __xdata *PUINT8X;
typedef const unsigned char __code *PUINT8C;
typedef unsigned char __xdata UINT8X;
typedef unsigned char  __data             UINT8D;

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "CH559.h"
#include "ticks.h"
#include "util.h"
#include "USBHost.h"
#include "uart.h"
#include "jjy.h"

SBIT(LED, 0x90, 6);


SBIT(JJY_N, 0xa0, 4); // P2.4
#define JJY (!JJY_N)


// ==== TICKS ====
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
// ================


uint8_t jjy_find_zero_bit()
{
    uint32_t start_sec = seconds();
    uint32_t begin_high = 0;
    while (seconds() - start_sec < 10) {
        uint32_t t = ticks();
        if ((t & 0x01) == 0) {
            if (JJY) {
                begin_high = t;
                break;
            }
        }
    }
    if (begin_high == 0) {
        // timeout
        return 255;
    }
    uint8_t bht = begin_high & 7;
    uint8_t last_high = 0;
    uint8_t high_count = 1;
    for (uint8_t i = 1; i < 15; i++) {
        while ((ticks() & 0x07) == bht) {}
        while ((ticks() & 0x07) != bht) {}
        uint32_t t = ticks();
        if (JJY) {
            last_high = i;
            high_count++;
        }
    }
    DEBUG_OUT("find_zero_bit: high duration = %d, high_count = %d\n", last_high, high_count);
    if ((last_high > 10) && (last_high < 13) && (high_count > 6)) {
        return (begin_high & 0x7f);
    }
    // not found
    return 255;
}

uint8_t jjy_detect_bit(uint8_t start_tick)
{
    while ((ticks() & 0x7f) != start_tick) {}
    uint8_t hc_0 = JJY;
    uint8_t hc_1 = 0;
    uint8_t hc_2 = 0;
    uint8_t hc_3 = 0;
    for (uint8_t i = 1; i < 31; i++) {
        while ((ticks() & 0x03) == (start_tick & 0x03)) {}
        while ((ticks() & 0x03) != (start_tick & 0x03)) {}
        if (JJY) {
            if (i < 7) {
                hc_0 ++;
            } else if (i < 17) {
                hc_1 ++;
            } else if (i < 26) {
                hc_2 ++;
            } else {
                hc_3 ++;
            }
        }
    }
    uint8_t n = 0;
    uint8_t r = 0;
    n |= (hc_0 >= 4) ? 1 : 0;
    n |= (hc_1 >= 6) ? 2 : 0;
    n |= (hc_2 >= 6) ? 4 : 0;
    n |= (hc_3 >= 4) ? 8 : 0;
    if (n == 1 || n == 9) r = 1;
    if (n == 3 || n == 2) r = 2;
    if (n == 7 || n == 6) r = 3;
    //DEBUG_OUT("detect_bit: %d,%d,%d,%d -> %d -> %d\n", hc_0, hc_1, hc_2, hc_3, n, r);
    return r;
}

static int8_t clock_send_command(const uint8_t *send_data, uint8_t len)
{
    uint8_t recv_data[32];
    if (FTDISend(send_data, len) < 0) {
        DEBUG_OUT("NC1: write failed.\n");
        return -1;
    } else {
        for (uint8_t retry = 0; retry < 10; retry++) {
            delay(100);
            int8_t l = FTDIReceive(recv_data, sizeof(recv_data));
            if (l < 0) {
                DEBUG_OUT("NC1: read failed.\n");
                return -1;
            } else if (l > 0) {
                if (recv_data[l - 3] != 'O' || recv_data[l - 2] != 'K' || recv_data[l - 1] != '!') {
                    DEBUG_OUT("NC1: invalid return data.\n");
                    return -1;
                } else {
                    DEBUG_OUT("NC1: command successful.\n");
                    return 0;
                }
            }
        }
        DEBUG_OUT("NC1: command response timed out.\n");
    }
    return -1;
}

void main()
{
    ticks_init();
    EA = 1;
    ET0 = 1;

    uint32_t t_last = 255;
    uint8_t clock_handshake_done = 0;
    /*uint8_t init_done = 0;
    uint8_t start_tick = 255;
    uint8_t time_data[8];
    uint8_t data_pos;
    uint8_t last_b; */
    uint8_t s;

    initClock();
    initUART0(1000000, 1);
    DEBUG_OUT("Startup\n");
    resetHubDevices(0);
    resetHubDevices(1);
    initUSB_Host();
    DEBUG_OUT("Ready\n");
//	sendProtocolMSG(MSG_TYPE_STARTUP,0, 0x00, 0x00, 0x00, 0);
    while (1) {
        if(!(P4_IN & (1 << 6)))
            runBootloader();

        checkRootHubConnections();
        if (FTDIIsConnected() && !clock_handshake_done) {
            uint8_t cmd_hello_data[] = { 'h', '9', '6', '9', '4' };
            if (clock_send_command(cmd_hello_data, sizeof(cmd_hello_data)) < 0) {
                DEBUG_OUT("NC1: handshake failed.\n");
                clock_handshake_done = 255;
            } else {
                DEBUG_OUT("NC1: handshake successful.\n");
                clock_handshake_done = 1;
            }
        }
        if (!FTDIIsConnected()) {
            clock_handshake_done = 0;
        }

        s = jjy_update(JJY);
        if (s == 1 && clock_handshake_done == 1) {
            uint8_t send_time_cmd[8];
            send_time_cmd[0] = 't';
            send_time_cmd[1] = '0';
            send_time_cmd[2] = '0' + (jjy_get_data(1) >> 4);
            send_time_cmd[3] = '0' + (jjy_get_data(1) & 0x0f);
            send_time_cmd[4] = '0' + (jjy_get_data(0) >> 4);
            send_time_cmd[5] = '0' + (jjy_get_data(0) & 0x0f);
            send_time_cmd[6] = '0';
            send_time_cmd[7] = '0';
            if (clock_send_command(send_time_cmd, sizeof(send_time_cmd)) < 0) {
                DEBUG_OUT("NC1: setting time failed.\n");
            } else {
                DEBUG_OUT("NC1: setting time successful.\n");
            }
            clock_handshake_done = 255;
        }
    }
#if 0
    while(1) {
        if(!(P4_IN & (1 << 6)))
            runBootloader();
        if (start_tick == 255) {
            DEBUG_OUT("finding zero bit...\n")
            start_tick = jjy_find_zero_bit();
            if (start_tick == 255) {
                DEBUG_OUT("zero bit not found.\n");
            } else {
                DEBUG_OUT("zero bit found, ticks: %d\n", start_tick);
                data_pos = 128;
                for (uint8_t i = 0; i < sizeof(time_data); i++) time_data[i] = 0;
                last_b = 0;
            }
        } else {
            uint8_t b = jjy_detect_bit(start_tick);
            if (b == 0) {
                DEBUG_OUT("...restart.\n");
                start_tick = 255;
            } else {
                if (data_pos == 128) {
                    if (b == 1 && last_b == 1) {
                        DEBUG_OUT("-> start bit found.\n");
                        data_pos = 0;
                        time_data[0] = 0;
                    } else {
                        data_pos = 127;
                    }
                } else if (data_pos < 9) {
                    const uint8_t v[] = { 0, 40, 20, 10, 0, 8, 4, 2, 1, 0 };
                    if (b == 2) time_data[0] += v[data_pos];
                } else if (data_pos == 9) {
                    DEBUG_OUT("-> minute=%d.\n", time_data[0]);
                } else if (data_pos < 19) {
                    const uint8_t v[] = { 0, 0, 20, 10, 0, 8, 4, 2, 1, 0 };
                    if (b == 2) time_data[1] += v[data_pos - 10];
                } else if (data_pos == 19) {
                    DEBUG_OUT("-> hour=%d.\n", time_data[1]);
                } else if (data_pos < 29) {
                    const uint8_t v[] = { 0, 0, 20, 10, 0, 8, 4, 2, 1, 0 };
                    if (b == 2) time_data[2] += v[data_pos - 20];
                } else if (data_pos == 29) {
                    ;
                } else if (data_pos < 35) {
                    const uint8_t v[] = { 8, 4, 2, 1, 0, 0};
                    if (b == 2) time_data[3] += v[data_pos - 30];
                } else if (data_pos == 35) {
                    DEBUG_OUT("-> yeardate=%d%d.\n", time_data[2], time_data[3]);
                } else if (data_pos < 39) {
                    const uint8_t v[] = { 2, 1, 0, 0, 0, 0};
                    if (b == 2) time_data[4] += v[data_pos - 36];
                } else if (data_pos == 39) {
                    DEBUG_OUT("-> parity=%d.\n", time_data[4]);
                } else if (data_pos < 49) {
                    const uint8_t v[] = { 0, 80, 40, 20, 10, 8, 4, 2, 1, 0 };
                    if (b == 2) time_data[5] += v[data_pos - 40];
                } else if (data_pos == 49) {
                    DEBUG_OUT("-> year=%d.\n", time_data[5]);
                } else if (data_pos < 53) {
                    const uint8_t v[] = { 4, 2, 1, 0, 0};
                    if (b == 2) time_data[6] += v[data_pos - 50];
                } else if (data_pos >= 53) {
                    DEBUG_OUT("-> dayofweek=%d.\n", time_data[6]);
                    start_tick = 255;
                }
                data_pos++;
                last_b = b;
            }
            //DEBUG_OUT("%d", b);
        }
    }
    while(1)
    {
        if(!(P4_IN & (1 << 6)))
            runBootloader();
        processUart();
        s = checkRootHubConnections();
        if (FTDIIsConnected() && !init_done) {
            FTDISend();
            delay(50);
            FTDIReceive();
            delay(500);
            FTDIReceive();
            delay(500);
            init_done = 1;
        } else if (!FTDIIsConnected()) {
            init_done = 0;
        }
        /*
        pollHIDdevice();
        FTDISend();
        delay(50);
        FTDIReceive();
        delay(500);
        FTDIReceive();
        delay(500);*/
        delay(50);
        if (ticks() / 128 != t_last) {
            t_last = ticks() / 128;
            DEBUG_OUT("**timer: %ld, JJY: %d\n", t_last, JJY);
        }
    }
#endif
}