// Templates the DUT macros used by ACT payloads and coverage monitors.
// SPDX-License-Identifier: Apache-2.0
#ifndef RHODIUM_ACT_RVMODEL_MACROS_H
#define RHODIUM_ACT_RVMODEL_MACROS_H

// @RVMODEL_ACCESS_FAULT_ADDRESS@

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

// SimpleSoC's ACLINT exposes hart 0's timer compare and the shared time counter.
// Its architectural timebase advances every clock cycle.
#define RVMODEL_INTERRUPT_LATENCY 1
#define RVMODEL_MTIMECMP_ADDRESS 0x02004000
#define RVMODEL_MTIME_ADDRESS 0x0200bff8
#define RVMODEL_MAX_CYCLES_PER_TIMER_TICK 1
#define RVMODEL_TIMER_INT_SOON_DELAY 5000

// SimpleSoC has no PMP entries, so every tested privilege can directly access
// its physical UART and PLIC windows without adding traps to the test stream.
#define RHODIUM_UART_IER 0x10000001
#define RHODIUM_PLIC_PRIORITY 0x0c000004
#define RHODIUM_PLIC_M_ENABLE 0x0c002000
#define RHODIUM_PLIC_S_ENABLE 0x0c002080
#define RHODIUM_PLIC_M_THRESHOLD 0x0c200000
#define RHODIUM_PLIC_M_CLAIM 0x0c200004
#define RHODIUM_PLIC_S_THRESHOLD 0x0c201000
#define RHODIUM_PLIC_S_CLAIM 0x0c201004
#define RHODIUM_WRITE32(_R1, _R2, _ADDRESS, _VALUE) \
  LI(_R1, _ADDRESS); LI(_R2, _VALUE); sw _R2, 0(_R1);
#define RHODIUM_WRITE8(_R1, _R2, _ADDRESS, _VALUE) \
  LI(_R1, _ADDRESS); LI(_R2, _VALUE); sb _R2, 0(_R1);
#define RHODIUM_SET_EXT_INT(_R1, _R2, _ENABLE, _OTHER_ENABLE, _THRESHOLD) \
  RHODIUM_WRITE32(_R1, _R2, RHODIUM_PLIC_PRIORITY, 1) \
  RHODIUM_WRITE32(_R1, _R2, _ENABLE, 2) \
  RHODIUM_WRITE32(_R1, _R2, _OTHER_ENABLE, 0) \
  RHODIUM_WRITE32(_R1, _R2, _THRESHOLD, 0) \
  RHODIUM_WRITE8(_R1, _R2, RHODIUM_UART_IER, 2)
#define RHODIUM_CLR_EXT_INT(_R1, _R2, _CLAIM) \
  LI(_R1, RHODIUM_UART_IER); sb zero, 0(_R1); \
  LI(_R1, _CLAIM); lw _R2, 0(_R1); sw _R2, 0(_R1);

#define RVMODEL_SET_MEXT_INT(_R1, _R2) \
  RHODIUM_SET_EXT_INT(_R1, _R2, RHODIUM_PLIC_M_ENABLE, RHODIUM_PLIC_S_ENABLE, RHODIUM_PLIC_M_THRESHOLD)
#define RVMODEL_CLR_MEXT_INT(_R1, _R2) RHODIUM_CLR_EXT_INT(_R1, _R2, RHODIUM_PLIC_M_CLAIM)
#define RVMODEL_CLR_MEXT_INT_M(_R1, _R2) RVMODEL_CLR_MEXT_INT(_R1, _R2)
#define RVMODEL_SET_SEXT_INT(_R1, _R2) \
  RHODIUM_SET_EXT_INT(_R1, _R2, RHODIUM_PLIC_S_ENABLE, RHODIUM_PLIC_M_ENABLE, RHODIUM_PLIC_S_THRESHOLD)
#define RVMODEL_CLR_SEXT_INT(_R1, _R2) RHODIUM_CLR_EXT_INT(_R1, _R2, RHODIUM_PLIC_S_CLAIM)
#define RVMODEL_CLR_SEXT_INT_M(_R1, _R2) RVMODEL_CLR_SEXT_INT(_R1, _R2)

#define RVMODEL_MSIP_ADDRESS 0x02000000
#endif
