// Forces striped LLC writeback and refill through the single external memory channel.
// SPDX-License-Identifier: Apache-2.0
.option norvc
.section .text
.globl _start
_start:
  // Far beyond the former 32 KiB RAM window, with distinct values in every
  // stripe and both halves of each cache line. The footprint exceeds all caches.
  li s0, 0x81000000
  li s1, 0x81010000
  mv t0, s0
write_lines:
  xor t1, t0, s1
  sd t1, 0(t0)
  not t1, t1
  sd t1, 56(t0)
  addi t0, t0, 64
  bltu t0, s1, write_lines
  fence rw, rw
  // Sweep a disjoint equal-sized region to evict the remaining dirty lines.
  li t0, 0x82000000
  li t2, 0x82010000
evict_lines:
  ld t1, 0(t0)
  addi t0, t0, 64
  bltu t0, t2, evict_lines
  mv t0, s0
read_lines:
  xor t1, t0, s1
  ld t2, 0(t0)
  bne t1, t2, fail
  not t1, t1
  ld t2, 56(t0)
  bne t1, t2, fail
  addi t0, t0, 64
  bltu t0, s1, read_lines
  // Also exercise the final line of the declared 1 GiB memory window.
  li t0, 0xbfffffc0
  li t1, 0x76543210
  sd t1, 56(t0)
  fence rw, rw
  ld t2, 56(t0)
  bne t1, t2, fail
  li t0, 1
  j finish
fail:
  li t0, 3
finish:
  la t1, tohost
  sd t0, 0(t1)
1:
  j 1b

.section .tohost,"aw",@progbits
.balign 8
.globl tohost
tohost: .dword 0
.globl fromhost
fromhost: .dword 0
