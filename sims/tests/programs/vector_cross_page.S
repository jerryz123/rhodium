/* Exercise crossing vector translations on each RVA23 core and precise store faults via MPRV. */
/* SPDX-License-Identifier: Apache-2.0 */
#include "pmp.h"
.option norvc
.section .text
.globl _start
_start:
  la t0, fail
  csrw mtvec,t0
  allow_test_memory
  li s7,0x20000
  li t0,0x1800
  csrc mstatus,t0
  li t0,0xe00  /* MPP=S, VS=Dirty. */
  csrs mstatus,t0
  li t0,0x8000000000080002
  csrw satp,t0
  sfence.vma
  /* Two adjacent VPNs, disjoint PPNs, and a third VPN aliasing only page two. */
  li s1,0x400ffc
  li s2,0x401000
  li s3,0x403000
  csrs mstatus,s7
  li t0,0x11111111
  sw t0,0(s1)
  li t0,0x22222222
  sw t0,0(s2)
  lw t0,0(s3)
  vsetivli zero,4,e8,m1,ta,ma
  vmv.v.i v8,7
  vse8.v v8,(s3)
  vsetivli zero,8,e8,m1,ta,ma
  vle8.v v4,(s1)
  vmv.x.s t1,v4
  li t0,17
  bne t1,t0,fail
  vslidedown.vi v6,v4,4
  vmv.x.s t1,v6
  li t0,7
  bne t1,t0,fail
  vmv.v.i v8,9
  vse8.v v8,(s1)
  lw t1,0(s1)
  lw t2,0(s2)
  li t0,0x09090909
  bne t1,t0,fail
  bne t2,t0,fail
  /* Revoke writes on page two. Loads still work; stores must fault precisely. */
  csrc mstatus,s7
  la t0,second_pte
  li t1,((0x8000c << 10) | 0x43)  /* V,R,A; no W. */
  sd t1,0(t0)
  fence rw,rw
  sfence.vma
  csrs mstatus,s7
  lw t1,0(s1)
  lw t2,0(s2)
  vle8.v v4,(s1)
  vslidedown.vi v6,v4,4
  vmv.x.s t1,v6
  li t0,9
  bne t1,t0,fail
  vmv.v.i v8,6
  la t0,expected_fault
  csrw mtvec,t0
faulting_store:
  vse8.v v8,(s1)
  j fail
.balign 4
expected_fault:
  csrc mstatus,s7
  csrr t0,mcause
  li t1,15
  bne t0,t1,fail
  csrr t0,mepc
  la t1,faulting_store
  bne t0,t1,fail
  csrr t0,mtval
  bne t0,s2,fail
  csrr t0,vstart
  li t1,4
  bne t0,t1,fail
  li t1,0x80008ffc
  lw t0,0(t1)
  li t1,0x06060606
  bne t0,t1,fail
  li t1,0x8000c000
  lw t0,0(t1)
  li t1,0x09090909
  bne t0,t1,fail
  li a0,1
  j finish
.balign 4
fail:
  li a0,3
finish:
  li t0,0x20000
  csrc mstatus,t0
  la t1,tohost
  sd a0,0(t1)
1: j 1b
.section .tohost,"aw"
.balign 64
.globl tohost,fromhost
tohost: .dword 0
.balign 64
fromhost: .dword 0
.section .pt,"aw"
.dword ((0x80003 << 10) | 1)  /* Root -> middle table. */
.org 0x1010
.dword ((0x80004 << 10) | 1)  /* VPN1=2 -> leaf table. */
.org 0x2000
.dword ((0x80008 << 10) | 0xc7)
second_pte:
.dword ((0x8000c << 10) | 0xc7), 0, ((0x8000c << 10) | 0xc7)
