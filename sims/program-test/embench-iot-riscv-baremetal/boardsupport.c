/* Supplies the board hooks required for functional Embench-IoT execution. */
/* SPDX-License-Identifier: Apache-2.0 */
#include "support.h"

static int board_errno;

int *__errno(void)
{
    return &board_errno;
}

void initialise_board(void)
{
}

void __attribute__((noinline, externally_visible)) start_trigger(void)
{
    __asm__ volatile("" ::: "memory");
}

void __attribute__((noinline, externally_visible)) stop_trigger(void)
{
    __asm__ volatile("" ::: "memory");
}
