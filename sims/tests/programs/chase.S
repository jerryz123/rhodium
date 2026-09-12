/* Traverses private mutable pointer rings striped over all eight SoC memory banks. */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    csrr a0, mhartid
    lui s11, 0x80000
    slli s11, s11, 32
    srli s11, s11, 32
    slli t0, a0, 14
    add s4, s11, t0
    addi s4, s4, 0x300
    ld s5, 0x200(s11)
    ld s6, 0x208(s11)
    slli t0, a0, 10
    add s0, s11, t0
    li t0, 0x800
    add s0, s0, t0
    slli s7, a0, 16
    li a2, 0
.Linitialize:
    srli t0, a2, 7
    slli t0, t0, 14
    andi t1, a2, 127
    slli t1, t1, 3
    add t0, t0, t1
    add t0, t0, s0
    slli t1, a2, 3
    slli t2, a2, 2
    add t1, t1, t2
    add t1, t1, a2
    addi t1, t1, 17
    andi t1, t1, 1023
    slli t2, a2, 4
    add t2, t2, a2
    add t2, t2, s7
    addi t2, t2, 1
    slli t2, t2, 32
    or t2, t2, t1
    sd t2, 0(t0)
    addi a2, a2, 1
    bltu a2, s5, .Linitialize
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
    csrr s3, minstret
    li s7, 0
    li s1, 0
    li s9, 0x7f4a7c15
    li s10, 0x9e3779b9
    slli a2, a0, 6
    slli t0, a0, 5
    add a2, a2, t0
    add a2, a2, a0
.Lround:
    mv s2, s5
.Lchase:
    srli t0, a2, 7
    slli t0, t0, 14
    andi t1, a2, 127
    slli t1, t1, 3
    add t0, t0, t1
    add a1, t0, s0
    ld a3, 0(a1)
    andi a2, a3, 1023
    srli a3, a3, 32
    addw a3, a3, s7
    andi t0, a3, 1
    beqz t0, .Leven
    slliw t0, a3, 5
    srliw t1, a3, 3
    xor a3, t0, t1
    xor a3, a3, s10
    j .Lstore
.Leven:
    slliw t0, a3, 7
    srliw t1, a3, 9
    addw a3, t0, t1
    addw a3, a3, s9
.Lstore:
    xor s7, s7, a3
    addiw s7, s7, 1
    slli a3, a3, 32
    or a3, a3, a2
    sd a3, 0(a1)
    addi s2, s2, -1
    bnez s2, .Lchase
    addi s1, s1, 1
    sd s1, 8(s4)
    bltu s1, s6, .Lround
    slli s7, s7, 32
    srli s7, s7, 32
    sd s7, 24(s4)
    csrr t0, minstret
    sub t0, t0, s3
    sd t0, 32(s4)
    csrr t0, mcycle
    sd t0, 48(s4)
    fence rw, rw
    li t0, 1
    sd t0, 16(s4)
.Lfinished:
    j .Lfinished
