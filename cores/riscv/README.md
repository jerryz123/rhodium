<!-- Describes reusable mappings between RISC-V instructions and processor components. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Reusable RISC-V component mappings

This package maps architectural RISC-V instructions onto reusable components
from `cores/`, such as the ALU, branch resolver, multiplier, and divider. It
does not define a complete processor pipeline or a named core's extension mix.

Named cores import these mappings and compose them with their own complete
decode relation.
