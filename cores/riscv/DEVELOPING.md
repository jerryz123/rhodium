<!-- Defines ownership and dependency rules for reusable RISC-V component mappings. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Reusable RISC-V mapping development

Files here may depend on architectural catalogs under `riscv/` and reusable
components directly under `cores/`. They must not import a named core such as
`cores/rv5stage/` or encode a named core's supported extension profile.

Keep complete instruction-set composition, pipeline controls, and
microarchitectural policy in the named core.
