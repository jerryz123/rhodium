# Exercises the mie/hie SGEIE alias before and after writes through both CSRs.
# SPDX-License-Identifier: Apache-2.0
.section .text
.globl _start
_start:
  csrw mideleg, zero
  li t0, -1
  csrw mie, t0
  csrr t1, mideleg
  csrr t2, mie
  csrr t3, 0x604
  csrw 0x604, zero
  csrr t4, mie
  csrw 0x604, t0
  csrr t5, mie
  csrr t6, 0x604
1: j 1b
