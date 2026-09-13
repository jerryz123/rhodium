// Terminates self-checking ACT payloads through the simulator's coherent HTIF mailboxes.
// SPDX-License-Identifier: Apache-2.0
#ifndef RHODIUM_ACT_RVMODEL_MACROS_H
#define RHODIUM_ACT_RVMODEL_MACROS_H

#define RVMODEL_DATA_SECTION \
  .pushsection .tohost,"aw",@progbits; \
  .balign 8; .global tohost; tohost: .dword 0; \
  .balign 8; .global fromhost; fromhost: .dword 0; \
  .popsection;

#define STANDARD_SM_SUPPORTED
#define RVMODEL_HALT_PASS \
  la t0, tohost; li t1, 1; sw zero, 4(t0); sw t1, 0(t0); 1: j 1b;
#define RVMODEL_HALT_FAIL \
  la t0, tohost; li t1, 3; sw zero, 4(t0); sw t1, 0(t0); 1: j 1b;

// The runner reports completion; console diagnostics require a future console binding.
#define RVMODEL_IO_INIT(_R1, _R2, _R3)
#define RVMODEL_IO_WRITE_STR(_R1, _R2, _R3, _STR_PTR)

// ACT requires these definitions even for I tests. Unexpected use must fail;
// real interrupt generation and timing belong to a later privileged adapter.
#define RVMODEL_INTERRUPT_LATENCY 1
#define RVMODEL_TIMER_INT_SOON_DELAY 1
#define RVMODEL_SET_MEXT_INT(_R1, _R2) RVMODEL_HALT_FAIL
#define RVMODEL_CLR_MEXT_INT(_R1, _R2) RVMODEL_HALT_FAIL
#define RVMODEL_SET_MSW_INT(_R1, _R2) RVMODEL_HALT_FAIL
#define RVMODEL_CLR_MSW_INT(_R1, _R2) RVMODEL_HALT_FAIL
#define RVMODEL_SET_SEXT_INT(_R1, _R2) RVMODEL_HALT_FAIL
#define RVMODEL_CLR_SEXT_INT(_R1, _R2) RVMODEL_HALT_FAIL
#define RVMODEL_SET_SSW_INT(_R1, _R2) RVMODEL_HALT_FAIL
#define RVMODEL_CLR_SSW_INT(_R1, _R2) RVMODEL_HALT_FAIL
#endif
