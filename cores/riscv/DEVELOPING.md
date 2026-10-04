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

`cosim.rhdl` owns implementation-neutral observation types and passive DPI
procedures. It uses only public Rhodium and architectural privilege types, not
the host receiver under `sims/cosim/`. Keep the flat ABI synchronized with that
receiver. Its owned behavioral integration test is `cosim-hooks` in
`sims/cosim/tests/circt/`; run it through the shared CIRCT runner after hook changes.
The clock argument may be explicit outside a synchronous domain or `#false`
inside one. The caller gates reset; hooks do not infer reset, acceptance, or
instruction ownership. The named-core producer has separate real-pipeline
fixtures under `cores/rv5stage/tests/`.

`cosim-source.rhm` is the authoring bridge for passive observations. It imports
the core metadata protocol and frontend read/domain APIs, stores local taps and
explicit child-instance views, and implements `IRRemappable`. Recipes may
capture detached host configuration only; all live hardware must appear in the
remapped taps. Compilation invokes recipes in a separate elaboration and checks
that observer circuits have only the declared inputs and no outputs. Component
taps must share the hart's clock and reset domain; a separately reset component
cannot silently reuse its hart's architectural epoch. This bridge
does not import a compiler pass, simulator, or named core.

## Focused validation

Run the mapping checks after changing their shared decode relations, or the
pure vector-layout check after changing row geometry:

```sh
tools/run-racket-tests.sh cores/riscv/tests/alu-decode-test.rhm cores/riscv/tests/multiply-decode-test.rhm cores/riscv/tests/divide-decode-test.rhm
tools/run-racket-tests.sh cores/tests/vector-layout-test.rhm
```

Run `bash cores/check-boundaries.sh` after changing imports or package layout.
The named core's DEVELOPING guide owns its composed decode and CHI fixtures.
