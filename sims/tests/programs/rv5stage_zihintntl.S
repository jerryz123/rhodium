// Checks translated NTL load non-allocation and dirty-resident preservation through real SoC/FESVR execution.
.option norvc
.section .text
.globl _start
_start:
  la t0, trap
  csrw mtvec, t0
  li s11, 2
  // M-mode executes the test, while MPRV selects S-mode Sv39 data accesses.
  // Identity mapping covers control data; a separate virtual gigapage aliases
  // the tested physical RAM. Bypassing translation would hit unmapped memory.
  la t0, root_page_table
  srli t0, t0, 12
  li t1, 8
  slli t1, t1, 60
  or t0, t0, t1
  csrw satp, t0
  sfence.vma
  li t0, 0x20800
  csrs mstatus, t0
  la s0, resident
  la s1, stream
  li t0, 0x40000000
  sub s0, s0, t0
  sub s1, s1, t0
  li s2, 0x123456789abcdef0
  la s7, begin_signature

  // Calibrate this concrete SoC's hit/miss gap instead of assuming absolute
  // cycle counts. Both addresses collide in the default direct-mapped L1Ds.
  li s3, -1
  li s4, -1
  li s8, 12
calibrate:
  mv a0, s0
  jal measure_plain
  mv a0, s1
  jal measure_plain
  bgeu a1, s4, 1f
  mv s4, a1
1:
  jal measure_plain
  bgeu a1, s3, 2f
  mv s3, a1
2:
  addi s8, s8, -1
  bnez s8, calibrate
  sd s3, 16(s7)
  sd s4, 24(s7)
  addi t0, s3, 8
  bgeu t0, s4, fail
  add s5, s3, s4
  srli s5, s5, 1
  sd s5, 32(s7)

  la s6, hint_functions
  li s10, 4
  csrr t0, misa
  andi t0, t0, 4
  beqz t0, 3f
  li s10, 8
3:
  sd s10, 8(s7)
  li s11, 16
hint_loop:
  ld a3, 0(s6)
  li s8, 12
  li s9, -1
  li s4, -1
trial:
  // Reestablish an authoritative dirty resident before every trial.
  sd s2, 0(s0)
  fence rw, rw
  mv a0, s1
  jalr a3
  li t0, 0x8877665544332211
  bne a2, t0, fail
  // A second hinted read must still miss: an ignored NTL would hit here.
  jalr a3
  li t0, 0x8877665544332211
  bne a2, t0, fail
  bgeu a1, s9, 4f
  mv s9, a1
4:
  // The conflicting dirty resident must still hit and retain its value.
  mv a0, s0
  jal measure_plain
  bne a2, s2, fail
  bgeu a1, s4, 5f
  mv s4, a1
5:
  addi s8, s8, -1
  bnez s8, trial
  sd s9, 40(s7)
  sd s4, 48(s7)
  bgeu s5, s9, fail
  bgeu s4, s5, fail
  addi s6, s6, 8
  addi s11, s11, 1
  addi s10, s10, -1
  bnez s10, hint_loop

  // SimpleSoC also routes a hinted FP load through the same translated LSU.
  csrr t0, misa
  andi t0, t0, 8
  beqz t0, finish
  li s11, 32
  li t0, 0x6000
  csrs mstatus, t0
  .word 0x00500033 // ntl.all
  .word 0x0004b007 // fld f0, 0(s1)
  .word 0xe2000553 // fmv.x.d a0, f0
  li t0, 0x8877665544332211
  bne a0, t0, fail
  li t0, 1
  sd t0, 56(s7)
finish:
  li t0, 0x123456789abcdef0
  sd t0, 0(s7)
  fence rw, rw
  li a0, 1
  j exit
trap:
  li t0, 0x20000
  csrc mstatus, t0
  la t3, begin_signature
  csrr t0, mcause
  sd t0, 40(t3)
  csrr t0, mepc
  sd t0, 48(t3)
  csrr t0, mtval
  sd t0, 56(t3)
  li s11, 63
fail:
  slli a0, s11, 1
  ori a0, a0, 1
exit:
  li t0, 0x20000
  csrc mstatus, t0
  la t0, tohost
  sd a0, 0(t0)
6:
  j 6b

// Identical, cache-line-aligned timing windows minimize instruction-side
// noise. Minima across trials tolerate unrelated coherent HTIF snoop traffic.
.macro measure name, encoding, compressed=0
.balign 64
\name:
  fence rw, rw
  rdcycle t0
  .if \compressed
    .hword \encoding
  .else
    .word \encoding
  .endif
  ld a2, 0(a0)
  fence rw, rw
  rdcycle t1
  sub a1, t1, t0
  ret
.endm
measure measure_plain, 0x00000013
measure measure_p1, 0x00200033
measure measure_pall, 0x00300033
measure measure_s1, 0x00400033
measure measure_all, 0x00500033
measure measure_cp1, 0x900a, 1
measure measure_cpall, 0x900e, 1
measure measure_cs1, 0x9012, 1
measure measure_call, 0x9016, 1

.section .rodata
.balign 64
hint_functions:
  .dword measure_p1, measure_pall, measure_s1, measure_all
  .dword measure_cp1, measure_cpall, measure_cs1, measure_call

.section .page_table,"aw",@progbits
.balign 4096
root_page_table:
  .dword 0, 0x200000cf, 0x200000cf
  .zero 509 * 8
.section .resident,"aw",@progbits
resident:
  .dword 0
.section .stream,"aw",@progbits
stream:
  .dword 0x8877665544332211
.section .signature,"aw",@progbits
.globl begin_signature, end_signature
begin_signature:
  .zero 64
end_signature:
.section .tohost,"aw",@progbits
.globl tohost, fromhost
tohost:
  .dword 0
fromhost:
  .dword 0
