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
| Upstream ISA/benchmark builds, manifests, execution, and simulator artifacts | [`program-test/`](program-test/) |
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

### Event export integration

The SimpleSoC harness owns transparent external-memory checkpoints. Keep them
outside synthesizable SoC code. `emit-event-harness.rhm` instruments one
elaboration, and `materialize-event-harness.rkt` saves its matching descriptor
and configured frequency alongside MLIR. The opt-in build links `rheg_dpi.cc`
with the independent RHEG libraries. Do not duplicate collector or encoder
logic in this adapter. `TestDriver.v` releases reset and observes completion on
falling edges; trace batches therefore follow all rising-edge callbacks.
Verilator's generated link rule omits user archives from its prerequisites.
When the outer simulator target is stale, `verilator/relink.mk` marks only the
generated executable target phony to force linking. Its model archive still
follows normal dependencies on both initial and incremental builds. Check both
an absent model archive and an archive-only library update when changing this rule.

Run the real smoke with native importer validation:

```sh
make -C sims trace-smoke TRACE_FILE=/tmp/simple-soc.pftrace \
  TRACE_PROCESSOR=/path/to/native/trace_processor_shell
```

`tests/check-event-trace.sh` requires request traffic and checks the two allowed
same-cycle edge families, paired payload/sequence equality, exact configured
timestamps, one-cycle slice widths, readable track labels, and importer errors.
It checks run timing once in the metadata table and site/capture context in
track descriptions, with no redundant run/site arguments on occurrences.
This optional test requires native Perfetto
and does not require RSP activity: the smoke's reads return data on DAT.
It is separate from ordinary simulation CI. `tests/check-event-driver.sh`
checks missing trace path, failed output open, and a small-cycle timeout with
an importable settled prefix. Also run untraced SimpleSoC smoke after changing
the common driver.

`tests/check-core-events.sql` additionally requires all five scalar pipeline
stages, exact permitted edge families, one parent per non-root event, no duplicate
children, matching RV64 PCs, and one-cycle downstream latency (elastic IF/ID may
take longer). It requires repeated fetched PCs to exercise distinct occurrences.
PC and instruction checks use named captures, independently of core bundle layout.
Memory pairs explicitly retain raw capture for their payload-equality checks.

### Other simulation contracts

The ACT flow is included from `arch-test/Makefile.inc`. Each
`arch-test/configs/<name>.mk` selects a UDB catalog entry, a simulator, and the
platform RAM window. Add future platforms through these entries; processor
extension policy stays in the owning core's UDB projection. The common
`configure.py` writes generated UDB consumer files, using the pinned Sail
default schema and explicit UDB mappings. Reject unsupported architecture
shapes before producing reference results. Always give ACT the full test
inventory and delegate extension closure and test constraints to it. Do not
maintain a separate suite selector. Replace only the configuration's generated
ELF outputs before each build so changes to core support cannot leave stale
tests for the upstream runner; preserve cached reference intermediates.
Reference/harness limitations remain distinct from core extension support;
extend and validate the projection as newly selected suites expose gaps.
Always enable privileged tests as well; missing platform hooks and reference
model mismatches must surface as build or execution failures, not suite exclusions.
Use ACT's keep-going mode to attempt every selected build even when others fail;
the generation command still returns failure if any build fails.

`arch-test/build.py` invokes the upstream CLI with its required Sail version
set to 0.14; the pinned ACT still requires 0.13.1. This keeps the upstream
version check active without modifying the submodule. Remove this compatibility
entry point when ACT adopts our Sail pin. The 0.14 projection also uses the
optional LR/SC exception encoding and clears H-only delegation bits when H is
disabled in UDB.

Generated YAML, Sail JSON, linker scripts, headers, ELFs, and logs stay in the
ACT build root. Keep the upstream submodule unmodified. When updating its
revision, check the required Sail version, bundled UDB gems, and header/runner
contracts together. The shared linker layout keeps test data addresses equal
between Sail signature payloads and self-checking DUT payloads; model-specific
text and HTIF mailboxes follow test data and stack.

Run `make -C sims arch-test-adapter-test program-test-adapter-test` for generation,
completion, deadlines, artifact identity, and complete-result checks using system
Python without ACT dependencies. CI runs these checks before workloads. With ACT
installed, run `make -C sims arch-test` to generate and execute all applicable
tests; the architecture-test CI lane uses this same target. When changing the driver, also run the existing smoke
and exercise a small `+max-cycles` timeout. See the
[operator guide](README.md#architectural-certification-tests) for setup and
current coverage limits.

### Software suite and artifact maintenance

`program-test/isa.mk` includes upstream build rules and selects their physical
test inventories. `build.py` owns benchmark selection, build flags, and
content-addressed ELF directories. Update selections for architecture or
execution-environment compatibility, never to hide failures. Keep sources in
the pinned submodule untouched. Compiler/source/adapter changes must invalidate
binary reuse; regenerate the manifest on every build invocation.

`program-test/run.py` owns ISA/benchmark process-group deadlines and JSON/JUnit
reporting. ACT retains upstream `run_tests.py`; `arch-test/report.py` checks its
summary against the full generated inventory. Never interpret an empty or partial
suite as success, and preserve the upstream runner's nonzero status independently
of reporting. Failed generation must stop before DUT execution.

ACT generation runs once in CI, separately from the native simulator build. It
publishes a checksum-verified archive with dereferenced ELF contents, so reference
build paths and upstream symlinks cannot leak into consumers. Four execution jobs
need only the native simulator, Python, and upstream runner, not Sail, Ruby, Racket,
or a compiler. `arch-test/shard.py` takes every sorted generated ELF and partitions
by index modulo shard count. Its tests enforce disjoint full coverage and safe
replacement of stale shard links. Shard inventories and results are artifacts;
all four matrix jobs must complete to claim full execution coverage.

The shared CI build publishes `VTestDriver` and its JSON attestation. Consumers
set `PREBUILT_SIMULATOR` to the downloaded executable. This bypasses native
build prerequisites and verifies commit, platform, SoC, and binary hash before
execution; missing artifacts must fail rather than silently build a replacement.
`artifact.py record` is run immediately after a successful simulator build in
the clean CI checkout. The executable uses statically linked FESVR and standard
Ubuntu runtime libraries; all producer/consumer jobs use the same runner image.

Keep tool downloads checksum-pinned and update compiler/ACT/Sail compatibility
together. Cache ACT reference products using generated configuration content,
upstream revisions, compiler, and adapter inputs. Generated ELF inventory is
still replaced on every ACT build. Keep result files out of binary caches.

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
make -C sims host-mmio-test SOC=simple
make -C sims host-mmio-test SOC=mini
make -C sims host-mmio-test SOC=tiled
make -C sims boot-test SOC=simple
make -C sims boot-test SOC=mini
make -C sims boot-test SOC=tiled
```

FESVR's write-data wrapper retains lane placement, masks, and packet-position
policy while using [`chi/messages.rhdl`](../chi/messages.rhdl) for immutable
`NonCopyBackWriteData` construction. Preserve its explicit DataID, CCID, and
DBID/MECID choices independently of the core requester profile.

The transport checks require the pinned FESVR library; DPI checks also require
Verilator. Lowering requires the pinned CIRCT tool or an explicit `CIRCT_OPT`,
and execution requires FESVR plus the RISC-V cross compiler. Rhombus checks use repository wrappers with fresh
compiled roots. Technology-mapped simulation remains owned by
[`../vlsi/sim/`](../vlsi/sim/README.md).

`transport-test` exercises the pinned FESVR `memif_t` path, exact-width and zero
writes, backpressure, and target errors. The backend `fesvr-mmio` fixture tests
the DPI-independent `FesvrCHIAccess` engine with coherent RAM fragmentation,
exact MMIO, response validation, and backpressure. `host-mmio-test` loads ELF
data into the UART scratch register, verifies the published ELF entry and
preserved UART value, and reads a device signature back through FESVR.

`DirectMemoryHtif::reset()` is FESVR's post-loading startup callback, not the
hardware reset signal. It validates the entry and publishes it with one blocking
eight-byte transaction before ordinary HTIF polling resumes. Every loading
transaction is also blocking, so no separate drain or boot arbitration state
machine is needed. Loading writes and clears overlapping the configured register
are rejected in C++; ordinary post-publication accesses remain allowed.
The register address and XLEN come from the harness through DPI, not native
constants or independent command-line settings. `FesvrRequester` connects the
transport directly to the generic `FesvrCHIAccess`; target/protocol failures
return through its normal response and become transport failure exits.

The native `transport-test` covers relocated registers, request/response stalls,
loading and publication failures, RV32/RV64 entries, zero entries, and overlapping
writes and clears. `boot-test` exercises actual FESVR and the indirect ROM at two
different ELF entry points on every SoC. These tests run in simulation CI.

The Zicboz payload checks all 64 offsets and neighboring blocks through the
normal FESVR flow. Its final signature also lets FESVR read the dirty cache
line coherently after the program exits:

```sh
make -C sims zicboz-test SOC=simple
```

`zihintntl-test` runs `tests/programs/rv5stage_zihintntl.S` through each ordinary
SoC harness. Keep its direct-mapped collision addresses consistent with the
default profiles. It calibrates warm and cold accesses on the running system,
then compares minima over repeated trials to tolerate unrelated HTIF snoops.
The second hinted read must remain a miss, while an intervening conflicting
dirty line must remain a hit. Do not weaken this to data-only checks: ignoring
NTL preserves architectural values and would otherwise pass. The payload
selects S-mode Sv39 data translation through MPRV while executing in M-mode;
test addresses are virtual aliases outside the physical RAM window, so bypassing
translation cannot pass. It needs no supervisor runtime. Compressed and FP
subcases are selected from `misa`. CI runs this target for simple, mini, and tiled.
