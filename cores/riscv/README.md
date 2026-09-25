<!-- Describes reusable RISC-V component mappings and implementation-neutral integration contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Reusable RISC-V component integration

This package maps architectural RISC-V instructions onto reusable components
from `cores/`, such as the ALU, branch resolver, multiplier, and divider. It
also owns [`chi-hart.rhdl`](chi-hart.rhdl), the implementation-neutral CHI
attachment description shared by RISC-V cores and SoCs. It does not define a
complete processor pipeline or a named core's extension mix.

Named cores import these mappings and compose them with their own complete
decode relation.

[`VectorRegisterLayout(vlen, row_bits)`](vector-layout.rhm) maps architectural
vector elements and mask bits into physical rows. Row width must divide VLEN;
an element must fit within one row. The layout exposes `rows_per_register`,
`depth` for all 32 registers, and element/mask locations as row and bit offset.
For VLEN=64, 32-bit rows give a 64-row bank; 64-bit rows give a 32-row bank.
This pure host model does not imply a port count, SRAM, or scheduling policy.
