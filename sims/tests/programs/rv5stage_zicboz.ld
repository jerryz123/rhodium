/* Places block-zero code, its coherent signature, and HTIF mailboxes in separate cache blocks. */
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
  . = 0x80000000;
  .text : { *(.text*) }
  . = ALIGN(64);
  .data : { *(.data*) }
  . = ALIGN(64);
  .tohost : { *(.tohost) }
}
