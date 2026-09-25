// Allows supervisor test accesses on harts with PMP, tolerating absent PMP CSRs.
// SPDX-License-Identifier: Apache-2.0

.macro allow_test_memory
  // No stack or core identity is needed. An unimplemented PMP CSR traps to
  // the continuation; other traps remain failures under the restored handler.
  la t0, .Lpmp_absent\@
  csrrw t1, mtvec, t0
  li t0, -1
  csrw pmpaddr0, t0
  li t0, 0x1f
  csrw pmpcfg0, t0
  j .Lpmp_done\@
.Lpmp_absent\@:
  csrr t0, mcause
  li t2, 2
  bne t0, t2, fail
.Lpmp_done\@:
  csrw mtvec, t1
.endm
