/* Places relocatable SoC smoke code and fixed HTIF mailboxes in memory. */
/* SPDX-License-Identifier: Apache-2.0 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
  . = DEFINED(payload_entry) ? payload_entry : 0x80000000;
  .text : { *(.text*) }

  . = 0x80001000;
  .tohost : { *(.tohost) }
}
