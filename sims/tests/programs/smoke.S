// Checks SoC boot, UART/PLIC, and selected vector geometry and execution through HTIF.
// SPDX-License-Identifier: Apache-2.0
#include "xlen.h"
.section .text
.globl _start
_start:
#ifdef RHODIUM_SMOKE_COMPRESSED_SENTINEL
  // Keep one explicit source encoding for the Perfetto disassembly regression.
  c.nop
#endif
  // HTIF failure codes identify the boot/assert, M claim, competing claim,
  // rearm, S claim, or final empty-claim stage (1 through 6).
  li s5, 1
  bnez a0, fail
  li t0, 0x10100
  bne a1, t0, fail
  LOAD_U32 t1, 0(a1)
  li t2, 0xedfe0dd0
  bne t1, t2, fail
  li t0, 0x1000
  LOAD_XLEN t1, 0(t0)
  la t2, _start
  bne t1, t2, fail
  // After handoff, exercise both word lanes without any concurrent ROM reader.
  // This is register readback coverage, not a warm-reboot protocol.
  li t3, 0x12345678
  sw t3, 4(t0)
  LOAD_U32 t4, 4(t0)
  bne t3, t4, fail
  sw zero, 0(t0)
  LOAD_U32 t4, 0(t0)
  bnez t4, fail
#if __riscv_xlen == 64
  ld t4, 0(t0)
  slli t3, t3, 32
  bne t3, t4, fail
#else
  sw zero, 4(t0)              // Restore the 64-bit register's zero high word on RV32.
#endif
  STORE_XLEN t1, 0(t0)
  LOAD_XLEN t4, 0(t0)
  bne t1, t4, fail

  // Assert the UART's level-sensitive TX-empty interrupt. Both contexts of
  // hart zero see source 1; global interrupt enables stay off while polling.
  li s0, 0x10000000
  li s1, 0x0c000000
  li t0, 1
  sw t0, 4(s1)                 // source 1 priority
  li t0, 2
  li t1, 0x2000
  add t1, s1, t1
  sw t0, 0(t1)                 // M context source 1 enable
  sw t0, 128(t1)               // S context source 1 enable
  sb t0, 1(s0)                 // UART IER: TX empty
  li s2, 0xa00                 // mip.MEIP | mip.SEIP
  li t2, 10000
plic_asserted:
  csrr t0, mip
  and t0, t0, s2
  beq t0, s2, plic_claim
  addi t2, t2, -1
  bnez t2, plic_asserted
  j fail
plic_claim:
  li s5, 2
  li s3, 0x200004
  add s3, s1, s3
  lw t0, 0(s3)                 // M claim consumes pending source globally
  li t1, 1
  bne t0, t1, fail
  li s4, 0x1000
  add s4, s3, s4
  li s5, 3
  lw t0, 0(s4)                 // S cannot claim the same in-flight source
  bnez t0, fail
  sb zero, 1(s0)               // deassert device before completion
  sw t1, 0(s3)
  li t2, 10000
plic_cleared:
  csrr t0, mip
  and t0, t0, s2
  beqz t0, plic_rearm
  addi t2, t2, -1
  bnez t2, plic_cleared
  j fail
plic_rearm:
  li s5, 4
  li t0, 2
  sb t0, 1(s0)                 // completed gateway accepts another assertion
  li t2, 10000
plic_reasserted:
  csrr t0, mip
  and t0, t0, s2
  beq t0, s2, plic_claim_supervisor
  addi t2, t2, -1
  bnez t2, plic_reasserted
  j fail
plic_claim_supervisor:
  li s5, 5
  lw t0, 0(s4)
  li t1, 1
  bne t0, t1, fail
  sb zero, 1(s0)
  sw t1, 0(s4)
  li s5, 6
  lw t0, 0(s3)
  bnez t0, fail
#ifdef RHODIUM_SMOKE_VECTOR
  // Fail explicitly on an illegal instruction, rather than looping at mtvec=0.
  li s5, 7
  la t0, fail
  csrw mtvec, t0
  li t0, 0x600                 // Enable vector architectural state.
  csrs mstatus, t0
  csrr t0, vlenb
  li t1, (__riscv_xlen / 4)    // Both RV32 presets have VLEN64; RVA23 has VLEN128.
  bne t0, t1, fail
  li s5, 8
  vsetvli t0, zero, e32, m1, ta, ma
  li t1, (__riscv_xlen / 16)
  bne t0, t1, fail
  vid.v v1
  vadd.vi v1, v1, 7
  la t0, vector_result
  vse32.v v1, (t0)
  vle32.v v2, (t0)
  vmv.v.i v3, 0
  vredsum.vs v3, v2, v3
  vmv.x.s t0, v3
#if __riscv_xlen == 32
  li t1, 15                   // 7 + 8, through the CHI memory path.
#else
  li t1, 34                   // 7 + 8 + 9 + 10.
#endif
  bne t0, t1, fail
  li s5, 9
  vmv.v.i v1, 1
  vbrev8.v v2, v1             // Zvbb is not just an advertised extension.
  vmv.x.s t0, v2
  li t1, 128
  bne t0, t1, fail
#if __riscv_xlen == 32
  li s5, 12
  vmv.v.i v1, 7
  li t1, 6
  vmul.vx v2, v1, t1         // Shared integer service, then dependent divide.
  vdiv.vx v3, v2, t1
  vmv.x.s t0, v3
  li t1, 7
  bne t0, t1, fail
  li s5, 13
  vsetvli t0, zero, e16, m1, ta, ma
  vmv.v.i v4, -3
  li t1, 7
  vwmul.vx v6, v4, t1        // Widen into native 32-bit result rows.
  vsetvli zero, zero, e32, m2, ta, ma
  vmv.x.s t0, v6
  li t1, -21
  bne t0, t1, fail
#endif
#ifdef __riscv_zve32f
  // Exercise shared scalar/vector F, result ownership, masks, flags, and memory.
  li s5, 14
  li t0, 0x6000
  csrs mstatus, t0
  csrw fcsr, zero
  vsetivli zero, 2, e32, m1, tu, mu
  li t0, 0x3fc00000           // 1.5 in binary32.
  fmv.w.x ft0, t0
  fadd.s ft1, ft0, ft0        // Scalar producer forwarded into vector snapshot.
  vfmv.v.f v1, ft1
  vmv.x.s t0, v1
  li t1, 0x40400000
  bne t0, t1, fail
  li s5, 17
  vfadd.vf v2, v1, ft0        // 4.5
  vmv.x.s t0, v2
  li t1, 0x40900000
  bne t0, t1, fail
  li s5, 18
  vfmul.vv v3, v2, v1        // 13.5
  vmv.x.s t0, v3
  li t1, 0x41580000
  bne t0, t1, fail
  li s5, 19
  vfdiv.vv v4, v3, v2        // 3.0 through the variable-latency shared service.
  vfmv.f.s ft2, v4
  fmv.x.w t0, ft2
  li t1, 0x40400000
  bne t0, t1, fail
  li s5, 15
  vfcvt.rtz.x.f.v v5, v3      // 13.5 -> 13, sets NX.
  vmv.x.s t0, v5
  li t1, 13
  bne t0, t1, fail
  csrr t0, fflags
  li t1, 1
  bne t0, t1, fail
  csrw fflags, zero
  vfcvt.f.x.v v6, v5
  vfmv.f.s ft2, v6
  fmv.x.w t0, ft2
  li t1, 0x41500000           // 13.0
  bne t0, t1, fail
  li s5, 16
  vid.v v7
  vmseq.vi v0, v7, 0
  vfadd.vv v6, v1, v1, v0.t  // First element becomes 6.0; second remains 13.0.
  la t0, vector_result
  vse32.v v6, (t0)
  lw t1, 0(t0)
  li t2, 0x40c00000
  bne t1, t2, fail
  lw t1, 4(t0)
  li t2, 0x41500000
  bne t1, t2, fail
  vle32.v v8, (t0)
  vmv.v.i v9, 0
  vfredosum.vs v10, v8, v9
  vfmv.f.s ft2, v10
  fmv.x.w t0, ft2
  li t1, 0x41980000           // 6.0 + 13.0 = 19.0.
  bne t0, t1, fail
  csrr t0, fflags
  bnez t0, fail
#endif
#ifdef __riscv_zve64d
  li s5, 10
  li t0, 0x6000               // Enable FP only on the FP-capable preset.
  csrs mstatus, t0
  vsetivli t0, 2, e64, m1, ta, ma
  li t1, 2
  bne t0, t1, fail
  li t0, 0x3ff0000000000000   // 1.0 in binary64.
  fmv.d.x ft0, t0
  vfmv.v.f v1, ft0
  vfadd.vv v2, v1, v1
  vfmv.f.s ft1, v2
  fmv.x.d t0, ft1
  li t1, 0x4000000000000000
  bne t0, t1, fail
#endif
#ifdef __riscv_zvfh
  li s5, 11
  li t0, 0x3f800000
  fmv.w.x ft0, t0
  fcvt.h.s ft2, ft0           // Scalar Zfh plus vector Zvfh.
  vsetivli zero, 8, e16, m1, ta, ma
  vfmv.v.f v1, ft2
  vfadd.vv v2, v1, v1
  vmv.x.s t0, v2
  li t1, 0x4000              // 2.0 in binary16.
  bne t0, t1, fail
#endif
#endif
  li t0, 1
  la t1, tohost
  STORE_XLEN t0, 0(t1)        // HTIF's high word stays zero in the ELF image.
1:
  j 1b

.balign 4
fail:
  slli t0, s5, 1
  ori t0, t0, 1
  la t1, tohost
  STORE_XLEN t0, 0(t1)
2:
  j 2b

.section .tohost,"aw",@progbits
.balign 8
.globl tohost
tohost:
  .dword 0
.globl fromhost
fromhost:
  .dword 0
#ifdef RHODIUM_SMOKE_VECTOR
.balign 16
vector_result:
  .space 16
#endif
