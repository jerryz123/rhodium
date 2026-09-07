// Verifies entry programming supersedes ELF device data and leaves an MMIO signature for FESVR.
.section .text
.globl _start
_start:
  li t0, 0x1000
  ld t1, 0(t0)
  la t2, _start
  bne t1, t2, fail
  li t0, 0x10000007
  lbu t1, 0(t0)
  li t2, 0xa5
  bne t1, t2, fail
  // This signature is deliberately in MMIO, not coherent RAM.
  li t0, 0x1000
  li t1, 0x123456789abcdef0
  sd t1, 0(t0)
  li t0, 1
  j done
fail:
  li t0, 3
done:
  la t1, tohost
  sd t0, 0(t1)
1:
  j 1b

.section .boot_register,"aw",@progbits
.dword 0x8877665544332211
.section .uart_scratch,"aw",@progbits
.byte 0xa5
.section .tohost,"aw",@progbits
.balign 8
.globl tohost
tohost: .dword 0
.globl fromhost
fromhost: .dword 0
