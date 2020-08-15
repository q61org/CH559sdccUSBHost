#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ticks.h"
#include "util.h"

static uint8_t g_time_data[8];
static uint8_t g_time_parity[2];
static uint8_t g_jjy_state = 0;

enum {
    JJY_STATE_FIND_ZERO_BIT_WAIT_HIGH = 0,
    JJY_STATE_FIND_ZERO_BIT_WAIT_LOW,
    JJY_STATE_READ_DATA_WAIT_SUBTICK,
    JJY_STATE_READ_DATA_WAIT_HIGH,
    JJY_STATE_READ_DATA_WAIT_LOW,
    JJY_STATE_READ_DATA_FIN,
};
#define JJY_STATE_INITIAL 0

enum {
    JJY_BIT_UNKNOWN = 0,
    JJY_BIT_ZERO = 1,
    JJY_BIT_ONE = 2,
    JJY_BIT_MARK = 3
};

volatile uint8_t jjy_get_data(uint8_t index)
{
    return (g_time_data[index & 0x07]);
}

static void jjy_increment_time_data()
{
    // add 1 minute
    g_time_data[0] += 1;
    if ((g_time_data[0] & 0x0f) <= 0x0a) return;
    // add 10 minutes
    g_time_data[0] = (g_time_data[0] + 0x10) & 0xf0;
    if (g_time_data[0] <= 0x60) return;
    // add 1 hour
    g_time_data[0] = 0;
    g_time_data[1] += 1;
    if ((g_time_data[1] & 0x0f) >= 0x0a) {
        g_time_data[1] = (g_time_data[1] + 0x10) & 0xf0;
    }
    if (g_time_data[1] <= 0x24) return;
    // todo add date
}

static int8_t jjy_record_data(uint8_t data_pos, uint8_t b)
{
    if (data_pos < 9) {
        const uint8_t v[] = { 0, 0x40, 0x20, 0x10, 0, 8, 4, 2, 1, 0 };
        if (b == JJY_BIT_ONE) {
            g_time_data[0] |= v[data_pos];
            g_time_parity[0] ++;
        }
    } else if (data_pos == 9) {
        DEBUG_OUT("JJY: minute: %02x\n", g_time_data[0]);
        g_time_data[1] = 0;
        if (b != JJY_BIT_MARK) {
            DEBUG_OUT("JJY: format error: not a mark bit (%d)\n", b);
            return -1;
        }
    } else if (data_pos < 19) {
        const uint8_t v[] = { 0, 0, 0x20, 0x10, 0, 8, 4, 2, 1, 0 };
        if (b == JJY_BIT_ONE) {
            g_time_data[1] |= v[data_pos - 10];
            g_time_parity[1] ++;
        }
    } else if (data_pos == 19) {
        DEBUG_OUT("JJY: hour: %02x\n", g_time_data[1]);
        g_time_data[2] = 0;
        if (b != JJY_BIT_MARK) {
            DEBUG_OUT("JJY: format error: not a mark bit (%d)\n", b);
            return -1;
        }
    } else if (data_pos < 29) {
        const uint8_t v[] = { 0, 0, 0x20, 0x10, 0, 8, 4, 2, 1, 0 };
        if (b == JJY_BIT_ONE) g_time_data[2] |= v[data_pos - 20];
    } else if (data_pos == 29) {
        g_time_data[3] = 0;
        if (b != JJY_BIT_MARK) {
            DEBUG_OUT("JJY: format error: not a mark bit (%d)\n", b);
            return -1;
        }
    } else if (data_pos < 35) {
        const uint8_t v[] = { 8, 4, 2, 1, 0, 0};
        if (b == JJY_BIT_ONE) g_time_data[3] |= v[data_pos - 30];
    } else if (data_pos == 35) {
        DEBUG_OUT("JJY: day of year: %02x%1x\n", g_time_data[2], g_time_data[3]);
        g_time_data[4] = 0;
    } else if (data_pos < 39) {
        const uint8_t v[] = { 2, 1, 0, 0, 0, 0};
        if (b == JJY_BIT_ONE) g_time_data[4] |= v[data_pos - 36];
    } else if (data_pos == 39) {
        DEBUG_OUT("JJY: parity: %x\n", g_time_data[4]);
        if ((g_time_parity[0] & 1) != (g_time_data[4] & 1)) {
            g_time_parity[0] = 255;
            DEBUG_OUT("JJY: parity check for minute failed.\n");
        }
        if ((g_time_parity[1] & 1) != ((g_time_data[4] & 2) >> 1)) {
            g_time_parity[1] = 255;
            DEBUG_OUT("JJY: parity check for hour failed.\n");
        }
        g_time_data[5] = 0;
        if (b != JJY_BIT_MARK) {
            DEBUG_OUT("JJY: format error: not a mark bit (%d)\n", b);
            return -1;
        }
    } else if (data_pos < 49) {
        const uint8_t v[] = { 0, 0x80, 0x40, 0x20, 0x10, 8, 4, 2, 1, 0 };
        if (b == JJY_BIT_ONE) g_time_data[5] |= v[data_pos - 40];
    } else if (data_pos == 49) {
        DEBUG_OUT("JJY: year: %02x\n", g_time_data[5]);
        g_time_data[6] = 0;
        if (b != JJY_BIT_MARK) {
            DEBUG_OUT("JJY: format error: not a mark bit (%d)\n", b);
            return -1;
        }
    } else if (data_pos < 53) {
        const uint8_t v[] = { 4, 2, 1, 0, 0};
        if (b == JJY_BIT_ONE) g_time_data[6] |= v[data_pos - 50];
    } else if (data_pos == 53) {
        DEBUG_OUT("JJY: day of week: %x\n", g_time_data[6]);
    } else if (data_pos == 59) {
        if (b == JJY_BIT_MARK) {
            jjy_increment_time_data();
            DEBUG_OUT("JJY: end of minute\n")
            return 1;
        }
    }
    return 0;
}

uint8_t jjy_update(uint8_t jjy)
{
    uint8_t rt = 0;

    // execute every 4 ticks == 32 times per second
    uint32_t t = ticks();
    if (t & 3) return rt;
    uint8_t ts = (t >> 2) & 0x1f;
    static uint8_t ts_last = 0;
    if (ts == ts_last) return rt;
    ts_last = ts;

    //DEBUG_OUT("JJY: update %d\r", ts);

    // state machine
    static uint32_t s_start_ticks = 0;
    static uint8_t s_start_subtick = 0;
    static uint8_t s_data_pos = 0;
    static uint8_t s_bit_start_subtick = 0;
    static uint8_t s_prev_low = 0;
    static uint8_t s_bit_read_try_count = 0;
    static uint8_t s_last_bit_type = 0;
    uint8_t wait_duration;

    switch (g_jjy_state) {
        case JJY_STATE_FIND_ZERO_BIT_WAIT_HIGH:
            if (jjy) {
                s_start_ticks = t;
                g_jjy_state = JJY_STATE_FIND_ZERO_BIT_WAIT_LOW;
                //DEBUG_OUT("JJY: got high, start_ticks = %ld\n", t);
            }
            break;
        case JJY_STATE_FIND_ZERO_BIT_WAIT_LOW:
            if (jjy && (t - s_start_ticks > 255)) {
                // timeout
                g_jjy_state = 0;
                break;
            }
            if (jjy) {
                break;
            } else {
                uint32_t high_duration = ((t - s_start_ticks) >> 2);
                if (high_duration >= 24 && high_duration <= 27) {
                    s_start_subtick = (s_start_ticks >> 2) & 0x1f;
                    DEBUG_OUT("JJY: found zero bit, subtick = %d\n", s_start_subtick);
                    s_data_pos = 255;
                    s_bit_read_try_count = 0;
                    g_jjy_state = JJY_STATE_READ_DATA_WAIT_SUBTICK;
                } else {
                    //DEBUG_OUT("JJY: got low, not zero bit: %ld\n", high_duration);
                    g_jjy_state = JJY_STATE_INITIAL;
                }
            }
            break;
        case JJY_STATE_READ_DATA_WAIT_SUBTICK:
            if (ts != s_start_subtick) break;
            g_jjy_state = JJY_STATE_READ_DATA_WAIT_HIGH;
            // fall through
        case JJY_STATE_READ_DATA_WAIT_HIGH:
            if (jjy) {
                s_bit_start_subtick = ts;
                s_prev_low = 0;
                g_jjy_state = JJY_STATE_READ_DATA_WAIT_LOW;
            } else {
                wait_duration = (ts - s_start_subtick) & 0x1f;
                if (wait_duration > 4) {
                    // timeout
                    DEBUG_OUT("JJY: timeout while waiting for high state: %d\n", wait_duration);
                    g_jjy_state = JJY_STATE_INITIAL;
                }
            }
            break;
        case JJY_STATE_READ_DATA_WAIT_LOW:
            if (!jjy) {
                if (s_prev_low) {
                    g_jjy_state = JJY_STATE_READ_DATA_FIN;
                } else {
                    s_prev_low = 1;
                }
            } else {
                s_prev_low = 0;
                wait_duration = (ts - s_start_subtick) & 0x1f;
                if (wait_duration > 29) {
                    // timeout
                    DEBUG_OUT("JJY: timeout while waiting for low state: %d\n", wait_duration);
                    g_jjy_state = JJY_STATE_INITIAL;
                }
            }
            break;
        case JJY_STATE_READ_DATA_FIN:
            {
                uint8_t high_duration = (ts - s_bit_start_subtick - 1) & 0x1f;
                uint8_t bit_type = JJY_BIT_UNKNOWN;
                if (4 <= high_duration && high_duration <= 9) {
                    bit_type = JJY_BIT_MARK;
                    DEBUG_OUT("JJY: MARK\n");
                } else if (11 <= high_duration && high_duration <= 19) {
                    bit_type = JJY_BIT_ONE;
                    DEBUG_OUT("JJY: 1\n");
                } else if (21 <= high_duration && high_duration <= 29) {
                    bit_type = JJY_BIT_ZERO;
                    DEBUG_OUT("JJY: 0\n");
                } else {
                    DEBUG_OUT("JJY: ??? (%d)\n", high_duration);
                }
                if (++s_bit_read_try_count > 180) {
                    DEBUG_OUT("JJY: tried 180 bits, resync zero bit\n");
                    g_jjy_state = JJY_STATE_INITIAL;
                    break;
                } else {
                    g_jjy_state = JJY_STATE_READ_DATA_WAIT_SUBTICK;
                }

                if (s_data_pos == 255) {
                    if (s_last_bit_type == JJY_BIT_MARK && bit_type == JJY_BIT_MARK) {
                        DEBUG_OUT("JJY: start bit found.\n");
                        s_data_pos = 1;
                        g_time_data[0] = 0;
                        g_time_parity[0] = 0;
                        g_time_parity[1] = 0;
                    }
                } else if (s_data_pos != 0) {
                    int8_t r = jjy_record_data(s_data_pos, bit_type);
                    if (r < 0) {
                        // out of sync
                        s_data_pos = 255;
                    } else if (r > 0) {
                        // complete data
                        s_data_pos = 255;
                        if ((g_time_parity[0] | g_time_parity[1]) != 255) {
                            DEBUG_OUT("JJY: data complete.\n");
                            while (((ticks() >> 2) & 0x1f) != ((s_start_subtick - 2) & 0x1f)) {}
                            rt = 1;
                        } else {
                            DEBUG_OUT("JJY: discarding data because parity check failed.\n");
                        }
                    } else {
                        if (++s_data_pos == 60) {
                            DEBUG_OUT("JJY: 60 bits read, but data was not complete?\n");
                            s_data_pos = 255;
                        }
                    }
                }
                s_last_bit_type = bit_type;
            }
            break;
    }
    return rt;
}
