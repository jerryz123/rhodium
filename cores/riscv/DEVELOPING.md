<!-- Defines ownership and dependency rules for reusable RISC-V component mappings and integration contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Reusable RISC-V integration development

Files here may depend on architectural catalogs under `riscv/` and reusable
components directly under `cores/`. Implementation-neutral attachment contracts
may also depend on shared protocols such as CHI. They must not import a named
core such as `cores/rv5stage/` or encode a named core's supported extension
profile or transaction implementation.

Keep complete instruction-set composition, pipeline controls, and
microarchitectural policy in the named core.

[`chi-hart.rhdl`](chi-hart.rhdl) owns physical-region/Home mapping, generic
instruction, data, and uncached requester capabilities, placement parameters,
and the hardware identity bundle. Named cores retain refill, writeback, snoop,
cache-maintenance, and uncached transaction state machines.
