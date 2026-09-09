// Qualifies constrained word/doubleword loops on real SoC memory paths, including multihart contention.
.option norvc
.option norelax
.section .text
.globl _start
_start:
  csrr s10, mhartid
  li t0, LRSC_HARTS
  bgeu s10, t0, park
  la t0, fail
  csrw mtvec, t0
  // The third placement must obey the production core's instruction alignment.
  csrr t0, misa
  andi t0, t0, 4
#if LRSC_IALIGN == 4
  bnez t0, fail
#else
  beqz t0, fail
#endif
#if LRSC_TRANSLATED
  li t0, 0x8000000000000000 | (LRSC_PAGE_TABLE_BASE >> 12)
  csrw satp, t0
  sfence.vma
  li t0, 0x800
  csrw mstatus, t0
  la t0, run_cases
  csrw mepc, t0
  mret
#endif
run_cases:
  li s5, 0
  la s6, functions
  la s7, arrivals
  slli t0, s10, 6
  add s7, s7, t0
  la s8, finished
  add s8, s8, t0
next_case:
  la s0, counters
  slli t0, s5, 6
  add s0, s0, t0
  addi s4, s5, 1
  sd s4, 0(s7)
  fence rw, rw
  la t0, arrivals
  jal barrier
  li s1, 8
  ld s2, 0(s6)
repeat_success:
  jalr s2
  addi s1, s1, -1
  bnez s1, repeat_success
  fence rw, rw
  sd s4, 0(s8)
  la t0, finished
  jal barrier
  bnez s10, advance
  ld t0, 0(s0)
  li t1, 8 * LRSC_HARTS
  bne t0, t1, fail
  la t1, begin_signature
  slli t2, s5, 3
  add t1, t1, t2
  sd t0, 0(t1)
advance:
  addi s5, s5, 1
  addi s6, s6, 8
  li t0, 6
  bltu s5, t0, next_case
  bnez s10, park
  li t0, 1
  j exit
barrier:
  li t1, LRSC_HARTS
1:
  ld t2, 0(t0)
  bltu t2, s4, 1b
  addi t0, t0, 64
  addi t1, t1, -1
  bnez t1, 1b
  fence rw, rw
  ret
fail:
  li t0, 3
exit:
  la t1, tohost
  sd t0, 0(t1)
park:
  wfi
  j park

// Sixteen contiguous instructions, including retry; the return is outside
// the constrained loop. The last placement uses the last legal instruction
// start before a page boundary: halfword with C, otherwise a complete word.
.macro constrained name, offset, size
  .org \offset
\name:
  lr.\size t0, (s0)
  .rept 6
    beq zero, zero, 1f
    nop
1:
  .endr
  addi t0, t0, 1
  sc.\size t1, t0, (s0)
  bnez t1, \name
  ret
.endm
.section .loops,"ax",@progbits
constrained word_aligned, 0xfc0, w
constrained word_cross, 0x1fe0, w
constrained word_boundary, (0x3000 - LRSC_IALIGN), w
constrained double_aligned, 0x3fc0, d
constrained double_cross, 0x4fe0, d
constrained double_boundary, (0x6000 - LRSC_IALIGN), d

.section .rodata
.balign 8
functions:
  .dword word_aligned, word_cross, word_boundary
  .dword double_aligned, double_cross, double_boundary
.section .signature,"aw",@progbits
.globl begin_signature, end_signature
begin_signature: .zero 48
end_signature:
.section .control,"aw",@progbits
.balign 64
counters: .zero 384
arrivals: .zero 512
finished: .zero 512
.section .tohost,"aw",@progbits
.globl tohost, fromhost
tohost: .dword 0
fromhost: .dword 0
.section .page_tables,"aw",@progbits
// Sv39 identity maps the first 2 MiB of RAM with independent 4-KiB leaves.
.zero 16
.dword ((LRSC_PAGE_TABLE_BASE + 0x1000) >> 2) | 1
.zero 4072
.dword ((LRSC_PAGE_TABLE_BASE + 0x2000) >> 2) | 1
.zero 4088
.set ppn, 0x200000cf
.rept 512
  .dword ppn
  .set ppn, ppn + 0x400
.endr
