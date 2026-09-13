<!-- Guides contributors through maintaining Rhodium physical-flow prototypes and handoffs. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing VLSI flows

Read the VLSI [README](README.md) for setup, staged commands, proof boundaries,
artifacts, and production limitations. This guide owns source placement,
physical-profile changes, artifact policy, and contributor validation.

## Architecture and ownership

This directory is an integration layer. It consumes public Rhodium/backend
outputs, the generic SRAM mapper, design-owned site policy, technology
catalogs, and an externally pinned OpenFrame harness. It may configure
physical tools and assert design/technology facts, but it must not redefine
Rhodium memory semantics, generic mapping schemas, SoC behavior, or simulator
transport.

Keep the two prototypes distinct:

- the OpenFrame path owns a small Rhodium leaf, wrapper compatibility, a
  compact LVS fixture, sparse hardening, and padframe cell swap;
- the MiniSoC path owns Sky130 site choices, manifest assertions, macro-aware
  RTL checks, Slang elaboration, and synthesis handoff.

Neither path may imply physical signoff beyond the exact stage that ran.

## Implementation map

| Concern | Owner |
|---|---|
| Build graph, tool selection, staged targets, and artifact assertions | [`Makefile`](Makefile) |
| Rhodium smoke leaf | [`src/rhodium-top.rhdl`](src/rhodium-top.rhdl) |
| Smoke-leaf and MiniSoC emitters | [`tools/emit-top.rhm`](tools/emit-top.rhm), [`tools/emit-mini-soc.rhm`](tools/emit-mini-soc.rhm) |
| OpenFrame boundary checker | [`tools/check-openframe-contract.py`](tools/check-openframe-contract.py) |
| MiniSoC mapping assertions | [`tools/check-mini-soc-memory-map.py`](tools/check-mini-soc-memory-map.py) |
| Native Yosys arithmetic-cone remapping and SAT proof after MiniSoC synthesis | [`openlane/mini_soc/post-synth.tcl`](openlane/mini_soc/post-synth.tcl) |
| MiniSoC/Sky130 site policy | [`designs/mini-soc/sky130/sram-map.yaml`](designs/mini-soc/sky130/sram-map.yaml) |
| OpenFrame wrapper and compact LVS RTL | [`verilog/rtl/`](verilog/rtl/) |
| LibreLane profiles | [`openlane/`](openlane/) |
| Generic selection, wrappers, and manifests | [`../sram/DEVELOPING.md`](../sram/DEVELOPING.md) |
| Mapped cycle simulation | [`sim/DEVELOPING.md`](sim/DEVELOPING.md) |

The `double_wide_openframe` submodule owns the empty wrapper contract, DEF pin
template, tool flake, padframe GDS, integration script, and physical environment.
Do not edit or regenerate those assets from this integration layer.

## Change a physical flow

1. Identify whether the change belongs to the reusable SRAM mapper, a
   technology catalog, design/technology policy, a physical profile, or the
   external harness contract.
2. Add a Make target at one clear proof boundary and assert its expected
   artifacts. Keep cheap checks ahead of synthesis, hardening, or integration.
3. Record the exact top, scope, policy, PDK, tool, and macro assumptions needed
   to reproduce the stage.
4. Keep physical-only choices out of synthesizable SoCs and generic mapping
   code. Put site selection and manifest assertions with the consuming design.
5. Describe only what the stage proves. RTL lint, synthesis, compact-fixture
   LVS, sparse hardening, and padframe cell swap are different claims.
6. Update [README.md](README.md) when setup, commands, outputs, supported flows,
   or proof limits change.

## Generated artifacts

Generated MLIR, SystemVerilog, inventories, manifests, plugin builds,
LibreLane runs, and exported views remain untracked under `vlsi/build/` or
`vlsi/openlane/*/runs/`. Checked-in sources are configurations, policies,
wrappers, emitters, checkers, and documentation. Never review a generated file
as though it were the source of design or technology policy.

The public README documents `make -C vlsi clean`; it intentionally removes only
flow-owned build products and run directories. Keep new outputs beneath those
owned roots so cleanup remains bounded.

## Focused validation

Follow the public staged workflow and stop at the narrowest stage covering the
change:

| Change | Focused target |
|---|---|
| Rhodium leaf, wrapper, or boundary checker | `make -C vlsi rtl-check` |
| Generic SRAM mapper | `make -C sram test` |
| MiniSoC policy or manifest assertions | `make -C vlsi mini-soc-memory-map` |
| Macro wrapper and installed Verilog handoff | `make -C vlsi mini-soc-macro-rtl-check` |
| Slang compatibility | `make -C vlsi mini-soc-slang-check` |
| Synthesis configuration | `make -C vlsi mini-soc-synth` |
| Targeted arithmetic post-mapping repair | `make -C vlsi mini-soc-post-synth` against a completed MiniSoC synthesis; also exercise the opt-in multiplier repair described in the README; check each SAT proof, unchanged outside RTLIL, structural checks, and 36-macro handoff |
| Compact physical/LVS fixture | `make -C vlsi lvs-smoke` |
| Sparse wrapper hardening | `make -C vlsi harden` |
| Padframe integration | `make -C vlsi integrate` |
| Complete current OpenFrame smoke | `make -C vlsi gds` |
| Mapped logical behavior | `make -C vlsi/sim smoke` |

Report unavailable PDK, Nix, LibreLane, Magic, FESVR, cross-compiler, CIRCT, or
Verilator dependencies as validation limits. Do not substitute a cheaper stage
for a stronger physical claim.
