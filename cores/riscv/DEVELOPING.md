<!-- Defines ownership and dependency rules for reusable RISC-V component mappings and integration contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Reusable RISC-V integration development

Read the package [README](README.md) for the component mappings, attachment
contract, and vector row layout. This guide owns source placement and
validation for their shared implementation boundary.

## Dependency direction

Files here may depend on architectural catalogs under `riscv/` and reusable
components directly under `cores/`. Implementation-neutral attachment contracts
may also depend on shared protocols such as CHI. They must not import a named
core such as `cores/rv5stage/` or encode a named core's supported extension
profile or transaction implementation.

Keep complete instruction-set composition, pipeline controls, and
microarchitectural policy in the named core.

## Implementation map

[`csr/`](csr/DEVELOPING.md) owns reusable CSR storage, privilege transitions,
and trap/interrupt machinery. Its command and configuration use architectural
types, not named-core configuration or pipeline bundles. Retirement events,
precise boundaries, and pipeline draining remain caller policy.

[`alu-decode.rhdl`](alu-decode.rhdl),
[`branch-decode.rhdl`](branch-decode.rhdl),
[`multiply-decode.rhdl`](multiply-decode.rhdl), and
[`divide-decode.rhdl`](divide-decode.rhdl) own the reusable component-control
relations. A named core completes its selected instruction domain and combines
the columns without importing another named core.

[`vector-layout.rhm`](vector-layout.rhm) owns physical vector-row mapping over the architectural
bit positions in `riscv/isa/vector.rhm`. Keep it pure host code and require an
explicit row width.

[`chi-hart.rhdl`](chi-hart.rhdl) owns physical-region/Home mapping, generic
instruction, data, and uncached requester capabilities, placement parameters,
and the hardware identity bundle. Named cores retain refill, writeback, snoop,
cache-maintenance, and uncached transaction state machines.

Typed DPI producers and native receivers belong to
[`sims/cosim/`](../../sims/cosim/DEVELOPING.md), not this package. Keep functional
core declarations independent of capture, simulator lifetime, and reference models.

`cosim-source.rhm` is the authoring bridge for passive observations. It imports
the core metadata protocol and frontend read/domain APIs, stores local taps and
explicit child-instance views, and implements `IRRemappable`. A hart declaration
contains a versioned contract string and immutable host configuration, not an
observer function or simulator-eligibility policy. All live hardware must appear
in the remapped taps. The simulation pass selects an adapter for that contract,
invokes it in a separate elaboration, and checks
that observer circuits have only the declared inputs and no outputs. Component
taps must share the hart's clock and reset domain; a separately reset component
cannot silently reuse its hart's architectural epoch. This bridge
does not import a compiler pass, simulator, or named core.

`describe_cosim_signals(..., ~components: {...})` may retain explicit immediate
children for nested component observations. The pass exports these only along
selected hart paths, recursively prefixes their tap names, and checks every
parent/child clock-reset boundary. No functional output ports are added during
ordinary elaboration. The pass fixture covers nested export and composition with
Flow instrumentation on both RTL backends.

An integration module can attach sibling taps to a child hart through
`describe_cosim_context(hart, components)`. The context retains explicit instance
references, not hierarchical string lookup. Only the selected child occurrence
receives added observation inputs; its simulation-owned adapter sees `component_field` names beside
local and child-component taps. Keep context components in the same clock/reset
domain. The integration owns the relationship, the component owns its semantic
tap timing, and the simulation-owned native adapter owns instruction correlation.

## Focused validation

Run the mapping checks after changing their shared decode relations, or the
pure vector-layout check after changing row geometry:

```sh
tools/run-racket-tests.sh cores/riscv/tests/alu-decode-test.rhm cores/riscv/tests/multiply-decode-test.rhm cores/riscv/tests/divide-decode-test.rhm
tools/run-racket-tests.sh cores/tests/vector-layout-test.rhm
```

Run `bash cores/check-boundaries.sh` after changing imports or package layout.
The named core's DEVELOPING guide owns its composed decode and CHI fixtures.
