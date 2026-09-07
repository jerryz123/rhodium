<!-- Guides contributors through maintaining executable SoC harnesses and simulator bindings. -->

# Developing simulation harnesses

Read the simulator [README](README.md) for selecting, building, and running a
harness and for the public execution boundary. This guide owns implementation
placement, build artifacts, bindings, and contributor validation.

## Architecture and ownership

Simulation consumes a synthesizable [`socs/`](../socs/README.md) composition
and supplies a parameterless top, coherent FESVR requester, optional
simulation-only memory, clock/reset driver, Verilator binding, and target
execution. Keep processor, device, CHI, NoC, and synthesizable-memory policy in
their owning packages. Keep DPI and target-loader behavior out of SoCs.

Each `SOC` selection maps to one harness module and one isolated build
directory. The shared emitter loads only that module and every variant exports
the same `SoCHarness` top contract. Preserve this isolation so switching
systems cannot reuse another system's generated RTL.

## Implementation map

| Concern | Owner |
|---|---|
| Build graph, tools, variants, and artifacts | [`Makefile`](Makefile) |
| Shared dynamic harness emitter | [`emit-soc-harness.rhm`](emit-soc-harness.rhm) |
| System-specific parameterless tops | [`simple-soc-harness.rhdl`](simple-soc-harness.rhdl), [`mini-soc-harness.rhdl`](mini-soc-harness.rhdl), [`tiled-soc-harness.rhdl`](tiled-soc-harness.rhdl) |
| Direct-memory FESVR transport and CHI requester | [`fesvr/`](fesvr/) |
| Verilator VPI/DPI binding | [`verilator/`](verilator/) |
| Clock, reset, UART pins, and exit | [`TestDriver.v`](TestDriver.v) |
| Harness checks and smoke payload | [`tests/`](tests/) |
| ACT4 configuration, reference-model projection, and execution adapter | [`arch-test/`](arch-test/) |
| CHI simulation memory | [`../chi/dpi-memory.rhdl`](../chi/dpi-memory.rhdl) and [`../chi/dpi/`](../chi/dpi/) |

## Add or change a harness

1. Keep the SoC instance and its hardware parameters in `socs/`; add only the
   parameterless execution wrapper and simulation-owned models here.
2. Preserve `SoCHarness` as the common generated top so `TestDriver.v` remains
   shared. Map the new `SOC` value to one explicit source rather than importing
   every system and selecting in hardware.
3. Give the variant a distinct build directory and declare every source,
   binding, header, and external library needed by that harness.
4. Keep FESVR's ELF loading and `tohost`/`fromhost` behavior in the host
   transport. The hardware side should expose only its narrow transaction and
   exit interfaces.
5. Add or extend the executable smoke for the new harness. A separate test that
   only inspects the elaborated hierarchy is not required; the simulator build
   already exercises elaboration and lowering. Update [README.md](README.md)
   when operators gain a new `SOC`, command, argument, or artifact location.

## Focused validation

The ACT flow is included from `arch-test/Makefile.inc`. Each
`arch-test/configs/<name>.mk` selects a UDB catalog entry, a simulator, and the
platform RAM window. Add future platforms through these entries; processor
extension policy stays in the owning core's UDB projection. The common
`configure.py` writes generated UDB consumer files, using the pinned Sail
default schema and explicit UDB mappings. Reject unsupported architecture
shapes before producing reference results. Extend the projection and its
validation before enabling other suites or privileged tests.

Generated YAML, Sail JSON, linker scripts, headers, ELFs, and logs stay in the
ACT build root. Keep the upstream submodule unmodified. When updating its
revision, check the required Sail version, bundled UDB gems, and header/runner
contracts together. The shared linker layout keeps test data addresses equal
between Sail signature payloads and self-checking DUT payloads; model-specific
text and HTIF mailboxes follow test data and stack.

Run `make -C sims arch-test-adapter-test` for completion-protocol regression
checks using system Python and no ACT dependencies; the simulation CI job runs
this target. Run `make -C sims arch-test` for real I-suite execution. The latter is
an explicit optional toolchain workflow; it is not added to routine CI in this
initial integration. When changing the driver, also run the existing smoke
and exercise a small `+max-cycles` timeout. See the
[operator guide](README.md#architectural-certification-tests) for setup and
current coverage limits.

Run host binding checks with:

```sh
make -C sims dpi-compile-check \
  VERILATOR_ROOT="$(verilator -V | sed -n 's/^ *VERILATOR_ROOT *= *//p' | head -1)"
make -C sims chi-dpi-memory-test \
  VERILATOR_ROOT="$(verilator -V | sed -n 's/^ *VERILATOR_ROOT *= *//p' | head -1)"
make -C sims transport-test
```

Check the tiled harness through CIRCT with:

```sh
make -C sims tiled-lowering-test
```

Run the end-to-end execution path for each supported system with:

```sh
make -C sims smoke SOC=simple
make -C sims smoke SOC=mini
make -C sims smoke SOC=tiled
```

The C++ checks require a Verilator installation, lowering requires the pinned
CIRCT tool or an explicit `CIRCT_OPT`, and execution requires FESVR plus the
RISC-V cross compiler. Rhombus checks use repository wrappers with fresh
compiled roots. Technology-mapped simulation remains owned by
[`../vlsi/sim/`](../vlsi/sim/README.md).

The Zicboz payload checks all 64 offsets and neighboring blocks through the
normal FESVR flow. Its final signature also lets FESVR read the dirty cache
line coherently after the program exits:

```sh
make -C sims zicboz-test SOC=simple
```
