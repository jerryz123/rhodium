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
