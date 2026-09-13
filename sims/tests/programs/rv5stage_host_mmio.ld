/* Keeps device ELF segments exact-sized so FESVR must preserve their MMIO widths. */
/* SPDX-License-Identifier: Apache-2.0 */
OUTPUT_ARCH(riscv)
ENTRY(_start)
PHDRS
{
  uart_scratch PT_LOAD FLAGS(6);
  ram PT_LOAD FLAGS(7);
}
SECTIONS
{
  .uart_scratch 0x10000007 : { *(.uart_scratch) } :uart_scratch
  .text 0x80000000 : { *(.text*) } :ram
  .tohost 0x80001000 : { *(.tohost) } :ram
  begin_signature = 0x1000;
  end_signature = 0x1008;
}
