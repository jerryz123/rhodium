// SPDX-License-Identifier: Apache-2.0
.section .text
.globl _start
_start:
  la t0, trap_handler
  csrw mtvec, t0
  li t6, 11
  ecall
  la t0, scratch
  li t1, 7
  li t2, 9
  mul t3, t1, t2
  sd t3, 0(t0)
  ld t4, 0(t0)
  li t5, 63
  bne t4, t5, fail
  div t4, t3, t1
  bne t4, t2, fail
  li t1, -1
  sb t1, 0(t0)
  lb t4, 0(t0)
  bne t4, t1, fail
  lbu t4, 0(t0)
  li t2, 255
  bne t4, t2, fail
  li t6, 5
  ld t3, 0(zero)
  li t1, 1
  j finish
fail:
  li t1, 3
finish:
  la t0, tohost
  sd t1, 0(t0)
1: wfi
  j 1b
.balign 4
trap_handler:
  csrr t0, mcause
  bne t0, t6, fail
  csrr t0, mepc
  addi t0, t0, 4
  csrw mepc, t0
  mret
.section .tohost,"aw",@progbits
.balign 64
.globl tohost
tohost: .dword 0
.globl fromhost
fromhost: .dword 0
scratch: .dword 0
