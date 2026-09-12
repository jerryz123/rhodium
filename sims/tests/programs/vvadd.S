/* Runs independent integer vector additions on every hart and publishes progress and results. */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    csrr a0, mhartid
    lui s0, 0x80000
    slli s0, s0, 32
    srli s0, s0, 32
    mv s11, s0
    slli t0, a0, 14
    add s0, s0, t0
    addi s4, s0, 0x300
    ld s5, 0x200(s11)
    ld s6, 0x208(s11)
    addi s1, s0, 0x400
    addi s2, s0, 0x640
    li t0, 0x880
    add s3, s0, t0
    slli s7, a0, 12
    li t0, 0
    mv t1, s1
    mv t2, s2
    mv t3, s3
.Linitialize:
    add t4, s7, t0
    sd t4, 0(t1)
    add t4, t4, t0
    addi t4, t4, 1
    sd t4, 0(t2)
    sd zero, 0(t3)
    addi t0, t0, 1
    addi t1, t1, 8
    addi t2, t2, 8
    addi t3, t3, 8
    bltu t0, s5, .Linitialize
    sd zero, 8(s4)
    sd zero, 16(s4)
    li t0, 1
    sd t0, 0(s4)
.Lwait_release:
    ld s8, 0x210(s11)
    beqz s8, .Lwait_release
.Lwait_cycle:
    csrr t0, mcycle
    bltu t0, s8, .Lwait_cycle
    sd t0, 40(s4)
    csrr s9, minstret
    li s10, 0
.Lround:
    mv t1, s1
    mv t2, s2
    mv t3, s3
    mv t0, s5
    li a1, 0
.Lvvadd:
    ld t4, 0(t1)
    ld t5, 0(t2)
    add t6, t4, t5
    sd t6, 0(t3)
    add a1, a1, t6
    addi t1, t1, 8
    addi t2, t2, 8
    addi t3, t3, 8
    addi t0, t0, -1
    bnez t0, .Lvvadd
    addi s10, s10, 1
    sd s10, 8(s4)
    bltu s10, s6, .Lround
    sd a1, 24(s4)
    csrr t0, minstret
    sub t0, t0, s9
    sd t0, 32(s4)
    csrr t0, mcycle
    sd t0, 48(s4)
    fence rw, rw
    li t0, 1
    sd t0, 16(s4)
.Lfinished:
    j .Lfinished
