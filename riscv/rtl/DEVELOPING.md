<!-- Guides contributors through maintaining the RISC-V-to-Rhodium adapter. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the RISC-V/Rhodium adapter

Read the adapter [README](README.md) for its public circuits, values, and
integration contracts. Read the parent
[`DEVELOPING.md`](../DEVELOPING.md) before changing the pure model or an ISA
catalog.

## Architecture and dependency boundary

`riscv/rtl` materializes pure architectural descriptions as reusable Rhodium
hardware. Adapter files may import `riscv/model`, `riscv/isa`, public Rhodium
libraries, and the public HardFloat package. They must not import Rhodium
implementation layers, backends, concrete cores, examples, or tests.

Keep representation conversion and architecture-wide reusable policy here.
Keep instruction selection, generated control, register files, privilege
transitions, scheduling, execution composition, and retirement in a concrete
core. [`../check-boundaries.sh`](../check-boundaries.sh) enforces this package
direction.

## Implementation map

| Concern | Owner |
|---|---|
| Encoding-to-pattern conversion | [`instruction-pattern.rhdl`](instruction-pattern.rhdl) |
| Field and immediate materialization | [`instruction-fields.rhdl`](instruction-fields.rhdl) |
| Compressed expansion | [`compressed.rhdl`](compressed.rhdl) |
| CSR values and bank construction | [`csr.rhdl`](csr.rhdl) |
| CMO privilege, WARL, and physical permission policy | [`cmo.rhdl`](cmo.rhdl) |
| Effective explicit-access privilege and pointer masking | [`privilege.rhdl`](privilege.rhdl), [`pointer-masking.rhdl`](pointer-masking.rhdl) |
| Base architectural counters | [`counters.rhdl`](counters.rhdl) |
| Trap and interrupt cause conversion | [`trap.rhdl`](trap.rhdl), [`interrupt.rhdl`](interrupt.rhdl) |
| Physical-memory attributes | [`pma.rhdl`](pma.rhdl) |
| Sv39 combinational helpers | [`sv39.rhdl`](sv39.rhdl) |
| RISC-V floating-point policy | [`floating-point.rhdl`](floating-point.rhdl) |
| Focused behavioral coverage | [`../tests/`](../tests/) |

## Change an adapter

1. Confirm that the behavior is reusable architectural representation or
   policy rather than core-specific microarchitecture.
2. Reuse the pure field, encoding, immediate, cause, CSR, profile, or geometry
   descriptor. Do not introduce a second opcode table or copied slice map.
3. Preserve host specialization for architectural profiles and widths so
   unsupported hardware elaborates away rather than becoming runtime control.
4. Use host tests under [`../tests/`](../tests/) for pure architectural policy,
   reference expansion, invalid inputs, and width/profile boundaries. Test
   cycle-visible circuit behavior in the corresponding backend fixture rather
   than inspecting internal elaborated shape.
5. Update [README.md](README.md) if the observable adapter contract changes.

Compressed-expansion changes must agree with pure host expansion for every
affected descriptor. CSR additions should leave privilege, alias, and WARL
policy with the layer that owns it. Floating-point helpers should translate
RISC-V policy around public HardFloat types without acquiring arithmetic
implementation.

## Focused validation

For pointer masking, run `riscv/tests/pointer-masking-test.rhm` and the
`riscv-pointer-masking` backend fixture. The latter sweeps PMM, MPRV/MPP,
MXR, Bare/virtual sign behavior, and disabled/RV32 specialization. Integrating
cores own policy capture, serialization, replay, and architectural fault tests;
RV5Stage covers those with `rv5stage-pointer-masking` and `rv5stage-csr`.

For CMO permission changes, select the `riscv-cmo` backend fixture. It sweeps
M/S/U privilege and both xenvcfg controls, RV32/RV64 WARL images, all Sv39 access
classes and low PTE permission/A/D combinations, and physical attributes.
Include `rv5stage-csr`, `rv5stage-zicboz`, and `rv5stage-mmu-replay` when shared
CSR or translation behavior changes. These fixtures check behavior, not IR
shape; use the [backend guide](../../tests/backend/DEVELOPING.md) for invocation.

From the repository root, run:

```sh
make riscv-test
```

The aggregate package target covers the model and catalogs that define the
adapter inputs, adapter behavior, compressed expansion, and dependency
boundaries. Repository wrappers provide a fresh `PLTCOMPILEDROOTS` unless the
caller supplies one; direct Racket or Rhombus runs must use a newly created
compiled root and `racket -y` where applicable.
