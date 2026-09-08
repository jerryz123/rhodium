// Exchanges every byte value through the UART pins and PTY, then exits through HTIF.
.section .text
.globl _start
_start:
  li s0, 0x10000000
  sb zero, 1(s0)               // no interrupts while polling
  li t0, 0x83
  sb t0, 3(s0)                 // DLAB
  li t0, 1
  sb t0, 0(s0)                 // divisor 1, matching the default PTY model
  sb zero, 1(s0)
  li t0, 3
  sb t0, 3(s0)                 // 8-N-1
  li t0, 7
  sb t0, 2(s0)                 // enable and clear both FIFOs
  li a0, 0x52                  // ready marker: R
  jal ra, transmit
  li s1, 0
exchange:
  jal ra, receive
  bne a0, s1, fail
  xori a0, a0, 0xa5
  jal ra, transmit
  addi s1, s1, 1
  li t0, 256
  bne s1, t0, exchange
  // Wait for the host to consume the final reply before closing the PTY.
  jal ra, receive
  li t0, 0x5a
  bne a0, t0, fail
  li t0, 1
  j done
fail:
  li t0, 3
done:
  la t1, tohost
  sd t0, 0(t1)
1:
  j 1b
receive:
  lbu t0, 5(s0)
  andi t1, t0, 0x1e           // overrun, parity, framing, or break
  bnez t1, fail
  andi t0, t0, 1
  beqz t0, receive
  lbu a0, 0(s0)
  ret
transmit:
  lbu t0, 5(s0)
  andi t0, t0, 0x20
  beqz t0, transmit
  sb a0, 0(s0)
  ret

.section .tohost,"aw",@progbits
.balign 8
.globl tohost
tohost: .dword 0
.globl fromhost
fromhost: .dword 0
