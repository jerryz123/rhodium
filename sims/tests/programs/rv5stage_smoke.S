// Checks BootROM handoff, the boot register, and UART-to-PLIC claim/completion through HTIF.
.section .text
.globl _start
_start:
  // HTIF failure codes identify the boot/assert, M claim, competing claim,
  // rearm, S claim, or final empty-claim stage (1 through 6).
  li s5, 1
  bnez a0, fail
  li t0, 0x10100
  bne a1, t0, fail
  lwu t1, 0(a1)
  li t2, 0xedfe0dd0
  bne t1, t2, fail
  li t0, 0x1000
  ld t1, 0(t0)
  la t2, _start
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

  // Assert the UART's level-sensitive TX-empty interrupt. Both contexts of
  // hart zero see source 1; global interrupt enables stay off while polling.
  li s0, 0x10000000
  li s1, 0x0c000000
  li t0, 1
  sw t0, 4(s1)                 // source 1 priority
  li t0, 2
  li t1, 0x2000
  add t1, s1, t1
  sw t0, 0(t1)                 // M context source 1 enable
  sw t0, 128(t1)               // S context source 1 enable
  sb t0, 1(s0)                 // UART IER: TX empty
  li s2, 0xa00                 // mip.MEIP | mip.SEIP
  li t2, 10000
plic_asserted:
  csrr t0, mip
  and t0, t0, s2
  beq t0, s2, plic_claim
  addi t2, t2, -1
  bnez t2, plic_asserted
  j fail
plic_claim:
  li s5, 2
  li s3, 0x200004
  add s3, s1, s3
  lw t0, 0(s3)                 // M claim consumes pending source globally
  li t1, 1
  bne t0, t1, fail
  li s4, 0x1000
  add s4, s3, s4
  li s5, 3
  lw t0, 0(s4)                 // S cannot claim the same in-flight source
  bnez t0, fail
  sb zero, 1(s0)               // deassert device before completion
  sw t1, 0(s3)
  li t2, 10000
plic_cleared:
  csrr t0, mip
  and t0, t0, s2
  beqz t0, plic_rearm
  addi t2, t2, -1
  bnez t2, plic_cleared
  j fail
plic_rearm:
  li s5, 4
  li t0, 2
  sb t0, 1(s0)                 // completed gateway accepts another assertion
  li t2, 10000
plic_reasserted:
  csrr t0, mip
  and t0, t0, s2
  beq t0, s2, plic_claim_supervisor
  addi t2, t2, -1
  bnez t2, plic_reasserted
  j fail
plic_claim_supervisor:
  li s5, 5
  lw t0, 0(s4)
  li t1, 1
  bne t0, t1, fail
  sb zero, 1(s0)
  sw t1, 0(s4)
  li s5, 6
  lw t0, 0(s3)
  bnez t0, fail
  li t0, 1
  la t1, tohost
  sd t0, 0(t1)
1:
  j 1b

fail:
  slli t0, s5, 1
  ori t0, t0, 1
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
