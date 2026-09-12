/* Places the position-independent shared workload below its coherent configuration words. */
ENTRY(_start)
SECTIONS {
    . = 0x80000000;
    .text : { *(.text*) }
    ASSERT(SIZEOF(.text) <= 0x200, "vvadd code overlaps configuration")
    /DISCARD/ : { *(.riscv.attributes) *(.comment) }
}
