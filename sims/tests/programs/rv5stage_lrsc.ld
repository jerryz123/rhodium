/* Places constrained loops and shared state, with Sv39 tables bounded by the selected SoC's RAM. */
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
  . = 0x80000000;
  .text : { *(.text) *(.rodata) }
  ASSERT(. <= 0x80001000, "qualification setup exceeds first page")
  . = 0x80001000;
  .loops : { *(.loops) }
  ASSERT(. <= 0x80008000, "qualification loops exceed placement region")
  . = 0x80008000;
  .signature : { *(.signature) }
  . = 0x80009000;
  .control : { *(.control) }
  . = 0x8000a000;
  .tohost : { *(.tohost) }
  ASSERT(. <= lrsc_page_tables, "qualification state overlaps page tables")
  . = lrsc_page_tables;
  .page_tables : { *(.page_tables) }
  ASSERT(. <= lrsc_memory_limit, "qualification exceeds SoC RAM")
}
