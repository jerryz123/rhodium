/* Implements CoreMark seeds and cycle timing for Rhodium's RV64 bare-metal target. */
/* SPDX-License-Identifier: Apache-2.0 */
#include "coremark.h"
#include "core_portme.h"

#ifndef RHODIUM_CLOCK_FREQUENCY_HZ
#error "RHODIUM_CLOCK_FREQUENCY_HZ must come from the concrete SoC target"
#endif

#ifndef PERFORMANCE_RUN
#error "The Rhodium CoreMark port uses the standard performance seeds"
#endif

volatile ee_s32 seed1_volatile = 0;
volatile ee_s32 seed2_volatile = 0;
volatile ee_s32 seed3_volatile = 0x66;

volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;
ee_u32 default_num_contexts = 1;

static CORETIMETYPE start_time_value;
static CORETIMETYPE stop_time_value;

static inline CORETIMETYPE read_cycle(void)
{
    CORETIMETYPE value;
    __asm__ volatile("csrr %0, mcycle" : "=r"(value));
    return value;
}

void start_time(void)
{
    start_time_value = read_cycle();
}

void stop_time(void)
{
    stop_time_value = read_cycle();
}

CORE_TICKS get_time(void)
{
    return stop_time_value - start_time_value;
}

secs_ret time_in_secs(CORE_TICKS ticks)
{
    return (secs_ret)(ticks / RHODIUM_CLOCK_FREQUENCY_HZ);
}

void portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc;
    (void)argv;
    if (sizeof(ee_ptr_int) != sizeof(void *) || sizeof(ee_u32) != 4)
        ee_printf("ERROR! Rhodium CoreMark platform type mismatch\n");
    p->portable_id = 1;
}

void portable_fini(core_portable *p)
{
    p->portable_id = 0;
}
