/* Starts a single-hart RV64 Bringup-Bench workload with bounded HTIF completion. */
/* SPDX-License-Identifier: Apache-2.0 */
    .section .text.init
    .globl _start
_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    la sp, __stack_top
    la t0, trap_entry
    csrw mtvec, t0
#ifdef __riscv_flen
    li t0, 0x6000
    csrs mstatus, t0
    csrwi fcsr, 0
#endif
#ifdef __riscv_vector
    li t0, 0x600
    csrs mstatus, t0
    csrwi vcsr, 0
#endif
    la t0, __bss_start
    la t1, __bss_end
1:
    bgeu t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:
    call main
    li a0, 3
    tail bringup_exit

    .balign 4
trap_entry:
    li a0, 4
    tail bringup_exit
