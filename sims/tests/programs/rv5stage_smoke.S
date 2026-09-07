// Checks the BootROM handoff and mapped boot-address register before reporting through HTIF.
.section .text
.globl _start
_start:
  bnez a0, fail
  li t0, 0x10100
  bne a1, t0, fail
  lwu t1, 0(a1)
  li t2, 0xedfe0dd0
  bne t1, t2, fail
  li t0, 0x1000
  ld t1, 0(t0)
  li t2, 0x80000000
  bne t1, t2, fail
  // After handoff, exercise both word lanes without any concurrent ROM reader.
  // This is register readback coverage, not a warm-reboot protocol.
  li t3, 0x12345678
  sw t3, 4(t0)
  lwu t4, 4(t0)
  bne t3, t4, fail
  sw zero, 0(t0)
  lwu t4, 0(t0)
  bnez t4, fail
  ld t4, 0(t0)
  slli t3, t3, 32
  bne t3, t4, fail
  sd t1, 0(t0)
  ld t4, 0(t0)
  bne t1, t4, fail
  li t0, 1
  la t1, tohost
  sd t0, 0(t1)
1:
  j 1b

fail:
  li t0, 3
  la t1, tohost
  sd t0, 0(t1)
2:
  j 2b

.section .tohost,"aw",@progbits
.balign 8
.globl tohost
tohost:
  .dword 0
.globl fromhost
fromhost:
  .dword 0
