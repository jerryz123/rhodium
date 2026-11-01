/* Aligns pristine upstream HTIF mailbox definitions to separate platform cache lines. */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RHODIUM_LITMUS_HTIF_H
#define RHODIUM_LITMUS_HTIF_H

#ifndef __ASSEMBLER__
#include <stdint.h>

extern volatile uint64_t tohost __attribute__((aligned(64)));
extern volatile uint64_t fromhost __attribute__((aligned(64)));
#endif

#endif
