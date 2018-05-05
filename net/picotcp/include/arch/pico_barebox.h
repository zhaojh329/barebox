#ifndef _INCLUDE_PICO_BAREBOX
#define _INCLUDE_PICO_BAREBOX

#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <clock.h>
#include <printk.h>
#include <linux/math64.h>

#define dbg pr_debug

#define pico_zalloc(x) calloc(x, 1)
#define pico_free(x) free(x)

static inline uint32_t PICO_TIME(void)
{
    uint64_t time;

    time = get_time_ns();
    do_div(time, 1000000000);

    return (uint32_t) time;
}

static inline uint32_t PICO_TIME_MS(void)
{
    uint64_t time;

    time = get_time_ns();
    do_div(time, 1000000);

    return (uint32_t) time;
}

static inline void PICO_IDLE(void)
{
}

#endif /* _INCLUDE_PICO_BAREBOX */
