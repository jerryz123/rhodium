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
Define hardware-only encoded choices directly with `hardware_enum`; do not add
a matching host enum and an enum-to-integer function solely to initialize it.
Keep pure host encodings when architectural models or tools consume them, such
as CSR addresses, trap/interrupt masks, MISA bits, and vector configuration.
Reusable mappings from instruction catalogs to shared processor components
belong in `cores/riscv/`; complete instruction selection, register files,
privilege-state storage, scheduling, execution composition, and retirement
belong in a concrete core. [`../check-boundaries.sh`](../check-boundaries.sh)
enforces this package direction.

## Implementation map

| Concern | Owner |
|---|---|
| Encoding-to-pattern conversion | [`instruction-pattern.rhdl`](instruction-pattern.rhdl) |
| Field and immediate materialization | [`instruction-fields.rhdl`](instruction-fields.rhdl) |
| Compressed expansion | [`compressed.rhdl`](compressed.rhdl) |
| CSR field materialization, values, operations, and bank construction | [`csr.rhdl`](csr.rhdl) |
| RISC-V decode-relation helpers | [`decode.rhdl`](decode.rhdl) |
| Vector architectural values and stateless configuration rules | [`vector.rhdl`](vector.rhdl); storage remains core-owned |
| Atomic operation values and update datapath | [`atomic.rhdl`](atomic.rhdl) |
| Zihintntl architectural locality selector | [`zihintntl.rhdl`](zihintntl.rhdl) |
| CMO privilege, WARL, and physical permission policy | [`cmo.rhdl`](cmo.rhdl) |
| Status trap/return transitions, effective explicit-access privilege, and pointer masking | [`privilege.rhdl`](privilege.rhdl), [`pointer-masking.rhdl`](pointer-masking.rhdl) |
| Base architectural counters | [`counters.rhdl`](counters.rhdl) |
| Stateless Sstc comparison and privilege gates | [`timer.rhdl`](timer.rhdl); comparator storage remains core-owned |
| Stateless state-enable CSR access policy | [`state-enable.rhdl`](state-enable.rhdl); implementation masks and state remain core-owned |
| Trap and interrupt selection, delegation, pending values, and cause conversion | [`trap.rhdl`](trap.rhdl), [`interrupt.rhdl`](interrupt.rhdl) |
| Physical-memory attributes | [`pma.rhdl`](pma.rhdl) |
| Sv39 combinational helpers | [`sv39.rhdl`](sv39.rhdl) |
| Svpbmt encodings, CSR masking, and effective access attributes | [`svpbmt.rhdl`](svpbmt.rhdl); physical permissions remain in `pma.rhdl` |
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

For guest execution contracts, pair `riscv/tests/hypervisor-test.rhm` and
`riscv/tests/csr-test.rhm` with the `rv5stage-hypervisor-csr` fixture.
Keep opcode/CSR catalogs pure, substitution/delegation helpers here, and
commit-owned state in the core. Add nested-translation request ownership and
continuation types with the walker consumer; do not infer fault provenance
from live CSR values after a request has been issued.
`rv5stage-nested-walker` exercises the captured guest context and Sv39x4
address helpers with the production standalone walker. Keep stage mode/root
geometry and permission representations here, but saved frames, serialized
memory ownership, and canceled-response draining in `cores/rv5stage/mmu/`.

For Svnapot and mapping geometry, run `rv5stage-svnapot` and
`rv5stage-mmu-replay`. They cover compact 64 KiB mappings, reserved encodings,
permissions, ordinary superpages, prefetch probes, and vector-window reuse.

The `rv5stage-svpbmt` fixture combines the reusable Svpbmt helpers with the
production walker/TLB and checks all PBMT encodings, PMA overrides, reserved
bits, held results, superpages, Bare bypass, and invalidation. Pair it with
`rv5stage-csr` for profile-controlled PBMTE writes and flush notification.

For pointer masking, run `riscv/tests/pointer-masking-test.rhm` and the
`riscv-pointer-masking` backend fixture. The latter sweeps PMM, MPRV/MPP,
MXR, Bare/virtual sign behavior, and disabled/RV32 specialization. The H sweep
adds independent senvcfg/henvcfg/HUPMM selection, explicit guest accesses,
MPRV/MPV, HS/VS MXR and HLVX exclusion to that same behavioral sweep. Integrating
cores own policy capture, serialization, replay, and architectural fault tests;
RV5Stage covers those with `rv5stage-pointer-masking` and `rv5stage-csr`.

For CMO permission changes, select the `riscv-cmo` backend fixture. It sweeps
M/HS/U/VS/VU privilege and all three xenvcfg controls, denial priority and
invalidate-to-flush conversion, RV32/RV64 WARL images, all Sv39 access
classes and low PTE permission/A/D combinations, and physical attributes.
Include `rv5stage-csr`, `rv5stage-zicboz`, and `rv5stage-mmu-replay` when shared
CSR or translation behavior changes. These fixtures check behavior, not IR
shape; pair with `rv5stage-hypervisor-csr` and `rv5stage-hypervisor-core` for
guest environment storage and precise denied-operation behavior. Use the
[backend guide](../../tools/testing/circt/DEVELOPING.md) for invocation.

From the repository root, run:

```sh
make riscv-test
```

The aggregate package target covers the model and catalogs that define the
adapter inputs, adapter behavior, compressed expansion, and dependency
boundaries. Repository wrappers provide the persistent worktree-specific
`PLTCOMPILEDROOTS` unless the caller supplies one; use the wrappers for direct
Racket or Rhombus execution.
