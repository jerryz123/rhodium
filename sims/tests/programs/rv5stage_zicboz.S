// Checks every block-zero address offset and exposes the final block to coherent FESVR signature reads.
// SPDX-License-Identifier: Apache-2.0
.section .text
.globl _start
_start:
  csrr t0, mhartid
  bnez t0, 1f
  la t0, trap
  csrw mtvec, t0
  li s0, 0
offset_loop:
  la t0, begin_signature
  li t1, 24
  li t2, -1
fill:
  sd t2, 0(t0)
  addi t0, t0, 8
  addi t1, t1, -1
  bnez t1, fill
  la a0, zero_block
  add a0, a0, s0
  .word 0x0045200f // cbo.zero (a0)
  fence rw, rw
  la t0, begin_signature
  li t1, 0
check_bytes:
  lbu t2, 0(t0)
  li t3, 64
  bltu t1, t3, expect_ones
  li t3, 128
  bgeu t1, t3, expect_ones
  bnez t2, fail
  j next_byte
expect_ones:
  li t3, 255
  bne t2, t3, fail
next_byte:
  addi t0, t0, 1
  addi t1, t1, 1
  li t3, 192
  bltu t1, t3, check_bytes
  addi s0, s0, 1
  li t0, 64
  bltu s0, t0, offset_loop
  li a0, 1
  j finish
trap:
fail:
  li a0, 3
finish:
  la t0, tohost
  sd a0, 0(t0)
1:
  j 1b

.section .data
.balign 64
.globl begin_signature
begin_signature:
  .fill 64, 1, 0xff
zero_block:
  .fill 64, 1, 0xff
  .fill 64, 1, 0xff
.globl end_signature
end_signature:

.section .tohost,"aw",@progbits
.balign 64
.globl tohost
tohost:
  .dword 0
.globl fromhost
fromhost:
  .dword 0
