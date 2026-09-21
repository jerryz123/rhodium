/* Reports Embench-IoT verification status through the standard HTIF exit word. */
/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>

volatile uint64_t tohost __attribute__((section(".tohost"), aligned(8)));
volatile uint64_t fromhost __attribute__((section(".tohost"), aligned(8)));

void __attribute__((noreturn)) embench_exit(int status)
{
    __asm__ volatile("fence rw, rw" ::: "memory");
    tohost = ((uint64_t)(uint32_t)status << 1) | 1;
    while (1)
        ;
}
