/* Adapts Bringup-Bench output and allocation to Rhodium's bare-metal HTIF ABI. */
/* SPDX-License-Identifier: Apache-2.0 */
#include "libmin.h"
#include <stdint.h>
#include <stddef.h>

#ifndef BRINGUP_EXPECTED_HASH
#error The workload must supply its checked-in expected output hash.
#endif

volatile uint64_t tohost __attribute__((section(".tohost"), aligned(8)));
volatile uint64_t fromhost __attribute__((section(".tohost"), aligned(8)));
volatile uint64_t __hashval = FNV64a_INIT;

extern unsigned char __heap_start[], __heap_end[];
static unsigned char *heap_break = __heap_start;

void __attribute__((noreturn)) bringup_exit(int status)
{
    __asm__ volatile("fence rw, rw" ::: "memory");
    tohost = ((uint64_t)(uint32_t)status << 1) | 1;
    while (1)
        ;
}

void __attribute__((noreturn)) libtarg_success(void)
{
    bringup_exit(__hashval == (uint64_t)BRINGUP_EXPECTED_HASH ? 0 : 2);
}

void __attribute__((noreturn)) libtarg_fail(int code)
{
    bringup_exit(code ? code : 1);
}

void libtarg_putc(char c)
{
    __hashval = libmin_fnv64a(&c, 1, __hashval);
}

void *libtarg_sbrk(size_t increment)
{
    intptr_t delta = (intptr_t)increment;
    unsigned char *previous = heap_break;
    if (delta < 0) {
        size_t decrement = (size_t)(-(delta + 1)) + 1;
        if (decrement > (size_t)(previous - __heap_start))
            return (void *)-1;
        heap_break = previous - decrement;
    } else {
        if ((size_t)delta > (size_t)(__heap_end - previous))
            return (void *)-1;
        heap_break = previous + delta;
    }
    return previous;
}

void *memcpy(void *destination, const void *source, size_t length)
{
    return libmin_memcpy(destination, source, length);
}

void *memmove(void *destination, const void *source, size_t length)
{
    return libmin_memmove(destination, source, length);
}

void *memset(void *destination, int value, size_t length)
{
    return libmin_memset(destination, value, length);
}
