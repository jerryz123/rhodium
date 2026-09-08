/* Separates NTL timing code, Sv39 root, conflicting L1D lines, signature, and HTIF in every default SoC's RAM. */
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
  . = 0x80000000;
  .text : { *(.text*) *(.rodata*) }
  ASSERT(. <= 0x80002000, "NTL timing code exceeds reserved instruction region")
  . = 0x80003000;
  .page_table : { *(.page_table) }
  . = 0x80004000;
  .resident : { *(.resident) }
  . = 0x80005000;
  .stream : { *(.stream) }
  . = 0x80006040;
  .tohost : { *(.tohost) }
  . = 0x80006100;
  .signature : { *(.signature) }
}
