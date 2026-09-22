// Qualifies OpenSBI's S-mode handoff, relocated FDT, and SBI system reset.
// SPDX-License-Identifier: Apache-2.0
.section .text
.globl _start
_start:
  bnez a0, fail
  la t0, expected_fdt
  bne a1, t0, fail
  lwu t0, 0(a1)
  li t1, 0xedfe0dd0
  bne t0, t1, fail

  // SBI base: GET_SPEC_VERSION must succeed and return a nonzero version.
  li a6, 0
  li a7, 0x10
  ecall
  bnez a0, fail
  beqz a1, fail

  // SBI base: PROBE_EXTENSION must report the TIME extension.
  li a0, 0x54494d45
  li a6, 3
  li a7, 0x10
  ecall
  bnez a0, fail
  beqz a1, fail

  // SBI base: PROBE_EXTENSION must report system reset before using it.
  li a0, 0x53525354
  li a6, 3
  li a7, 0x10
  ecall
  bnez a0, fail
  beqz a1, fail

  // OpenSBI owns HTIF termination; the S-mode payload exits through SBI SRST.
  li a0, 0
  li a1, 0
  li a6, 0
  li a7, 0x53525354
  ecall

fail:
  wfi
  j fail
