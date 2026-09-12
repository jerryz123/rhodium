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

Import the CHI owners used by each simulator component directly. FESVR consumes
wire, channel, service, and message contracts; the SimpleSoC harness explicitly
imports `chi/subordinate/memory-controller.rhdl` and `chi/subordinate/dpi-memory.rhdl`. Neither needs the
all-CHI facade. The [CHI import guide](../chi/README.md#package-boundary-and-import)
owns the public entry-point contract.

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
| Native abstraction registry and C SoC runner | [`native/DEVELOPING.md`](native/DEVELOPING.md) |
| All-hart benchmark wrapper and targets | [`banked-soc-harness.rhdl`](banked-soc-harness.rhdl), [`native/BENCHMARKS.md`](native/BENCHMARKS.md) |
| Verilator VPI/DPI binding | [`verilator/`](verilator/) |
| Clock, reset, and exit | [`TestDriver.v`](TestDriver.v) |
| PTY transport and serial conversion reused by every harness | [`../devices/uart-dpi.rhdl`](../devices/uart-dpi.rhdl), [`../devices/dpi/uart_dpi.cc`](../devices/dpi/uart_dpi.cc) |
| Harness checks and smoke payload | [`tests/`](tests/) |
| ACT4 configuration, reference-model projection, and execution adapter | [`arch-test/`](arch-test/) |
| Upstream ISA/benchmark builds, manifests, execution, and simulator artifacts | [`program-test/`](program-test/) |
| CHI simulation memory | [`../chi/subordinate/dpi-memory.rhdl`](../chi/subordinate/dpi-memory.rhdl) and [`../chi/subordinate/dpi/`](../chi/subordinate/dpi/) |

## Add or change a harness

Each `CHIDPIMemory` registers its C++ backing store on clocked reset edges,
using its own model ID, configured capacity, and physical identity input.
There is no separate harness descriptor or generated registration header.
The DPI scope identifies the owning instance: repeated reset registration is
idempotent, while duplicate IDs, changed windows, and overlapping ranges fail.

`fesvr/image_memory.h` owns protocol-independent range splitting and callbacks.
`verilator/chi_image_memory.h` consumes the CHI-owned native registry without
making CHI depend on FESVR. `image_memory_map.cc` selects the native backends
linked by the simulator build; systems without native RAM have an empty map.
The first non-reset HTIF tick freezes the registry before loading. All RAMs
must receive clocked reset before that edge; the shared driver supplies three
reset edges and deasserts on a falling edge. This avoids dependence on callback
ordering within an edge. Initialization errors remain fatal to loading, and new
registrations after the freeze fail. Register only RAM that is safe to mutate during
cold loading. Callback failures are fatal, never retried through CHI. Runtime
accesses bypass the registration map after FESVR's startup reset callback.
Validate both ordinary and `+load-through-chi` execution when changing loading.

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
logic in this adapter. The adapter selects gzip only for a `.gz` output suffix and calls
the exporter's checked `finish()` before closing the file on exit or timeout.
`TestDriver.v` releases reset and observes completion on
falling edges; trace batches therefore follow all rising-edge callbacks.
Bind descriptor and timing before callbacks, and flush the final settled cycle
before normal exit or timeout. Keep emitter, generated clock constant, descriptor,
and RTL tied to the same harness configuration.
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
timestamps, one-cycle transfers, continuous stall ranges, readable track labels,
and importer errors.
It checks run timing once in the metadata table and site/capture context in
track descriptions, with no redundant run metadata on occurrences. SimpleSoC
instruction and CHI transfer names cannot equal `stall`, so these checks use
that slice name to separate stalls on shared tracks. Do not require exact graph
identity reconstruction from the Perfetto visualization.
This optional test requires native Perfetto
and does not require RSP activity: the smoke's reads return data on DAT.
It is separate from ordinary simulation CI. `tests/check-event-driver.sh`
checks missing trace path, failed output open, and a small-cycle timeout with
an importable settled prefix. Also run untraced SimpleSoC smoke after changing
the common driver.

`tests/check-core-events.sql` additionally requires all five scalar pipeline
stages, exact permitted edge families, one parent per downstream scalar event, no duplicate
children, matching RV64 PCs, and one-cycle downstream latency (elastic IF/ID may
take longer). It requires repeated fetched PCs to exercise distinct occurrences.
PC and instruction checks use named captures, independently of core bundle layout.
`check-frontend-events.sql` connects request, lookup, outcome, and core-fetch
tracks. Core fetch and its stalls have one or two retained, admitted S2
parents; they are no longer roots. S2 captures admission and fault flags in the
single outcome event, and instruction consumption follows at least one cycle
later. The cycle-level `event-frontend` fixture owns exact compressed/straddle
parent reconstruction; the smoke checks importer-visible edge families.
Select stages through track names, not mnemonic slice names, and check full
disassembly separately from the mnemonic. Generic display/schema rules belong
to [RHEG](../rheg/DEVELOPING.md#perfetto-encoding), not this adapter.
Memory pairs explicitly retain raw capture for their payload-equality checks.
The D-cache stage checks in `tests/check-demand-events.sql` pair S1 and S2
through their MEM/WB parents and require one-cycle correspondence. They check
admitted S2 ancestry into S3, one-cycle S3-to-S4 advancement, and S4 refill
acceptance fields. Keep effective S1/S2 addresses separate from physical S3/S4
addresses; translation need not preserve their numeric value. Direct S4 refill
acceptance must reach TXREQ with matching opcode/line address; retries may
produce multiple children. Explicitly detached traffic may have no S4 parent.
Restrict those pipeline checks to transfer sites. `tests/check-stall-events.sql`
requires real fetch/decode backpressure, matching capture layouts, the exact
Boolean hazard fields, and accepted-fetch parents for decode stalls. It checks
that surviving issue follows the end of its stalls while still inheriting the fetch parent,
and rejects outgoing edges from any stall observation. Keep these checks separate
from transfer fanout and fixed-latency rules. Check durations, non-overlap, and
captured reasons on each coalesced stall slice.

`tests/check-cache-events.sql` checks observed private-cache CHI descriptors
(the importer omits idle channels from its track table),
exact named capture layouts, actual I/D request and refill-data activity, node
identity, decoded opcode slice names against per-channel enum metadata, successful
response status, byte-addressed snoops, and isolated event
lineage at these opaque transaction boundaries. Keep scalar and external-memory
checks scoped to their own tracks when adding cache channels. See the
[RV5Stage annotation owner](../cores/rv5stage/DEVELOPING.md#pipeline-event-annotations).
Validate cache stall slices on the original channel tracks, always named `stall`,
with the same per-channel captures and opcode tables as transfers; never require
a quiet channel to stall. RN-I
instruction requests use nonsnooping `ReadOnce` snapshots; preserve that
transaction distinction when checking expected traffic.

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

`Za64rs` and `Za128rs` are reservation bounds, not Sail extension switches.
Validate their versions and bounds against Sail's naturally aligned reservation
size without enlarging it. The pinned default is eight bytes: a conforming
reference choice, not an assertion that it exactly reproduces the DUT's
access-sized LR.W reservation. `Zic64b` projects and checks a 64-byte cache block
even when CBO instruction extensions are disabled. Keep these cases in the
adapter tests so profile guarantees cannot silently bypass platform validation.

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

### LR/SC system qualification

`make -C sims lrsc-test SOC=simple`, `SOC=mini`, and `SOC=tiled` complement the
[full-core progress matrix](../cores/rv5stage/DEVELOPING.md#ziccrse-progress-gate).
`tests/programs/rv5stage_lrsc.S` runs six constrained-loop placements, covering
LR.W/SC.W and LR.D/SC.D at aligned, cross-line/page, and page-boundary starts. Each
loop contains sixteen contiguous instructions including its retry branch.
Setup, barriers, function returns, signatures, and HTIF exit are outside the
constrained loop. The linker keeps code, shared counters, and page tables
separate. Bare and supervisor Sv39 executables use the same identity-mapped
RAM, with independent 4-KiB leaf mappings and preset A/D bits. MiniSoC places
its page tables inside its 64-KiB RAM and uses word-aligned boundary starts;
the compressed-enabled systems use halfword starts. The payload checks `misa.C`
against the selected alignment, and the linker rejects out-of-RAM placement.

SimpleSoC and MiniSoC use their ordinary harnesses. `tests/lrsc-tiled-harness.rhdl` changes
only the production ROM's secondary-hart filter to a NOP. Every hart still
waits for the normal FESVR post-loading entry publication. Do not replace the
cores, Home slices, routers, translation, cache geometry, or FESVR transport
with fixture components. The eight-hart test contract is checked against the
default topology before elaboration.

All participating harts perform eight successful increments per placement.
Finite winners leave each loop, so the all-hart completion check does not
require starvation freedom against an indefinitely active winner. Shared
counters occupy consecutive cache lines across the tiled Home stripes.
Separate per-hart barrier lines keep setup stores outside the tested
reservation lines. Hart zero checks totals and writes six signature values;
FESVR reads those values coherently after normal HTIF completion.

Keep these builds isolated under `BUILD_ROOT/lrsc-test/`; the tiled boot ROM
differs from the ordinary simulator. Both Bare and Sv39 executions must be
attempted, with a nonzero overall status if either fails. Preserve failed
results and do not count timeout, post-pressure recovery, or partial
signatures as qualification success. This finite regression is evidence for
the concrete configurations, not an advertisement switch or a proof for
arbitrary external fabric fairness.

### Software suite and artifact maintenance

`program-test/isa.mk` includes upstream build rules and selects their physical
test inventories. `build.py` owns benchmark selection, build flags, and
content-addressed ELF directories. Update selections for architecture or
execution-environment compatibility, never to hide failures. Keep sources in
the pinned submodule untouched. Compiler/source/adapter changes must invalidate
binary reuse; regenerate the manifest on every build invocation.

`program-test/write-target.rhm` projects the existing concrete SoC description
to the ISA smoke adapter; do not duplicate ISA or RAM constants in Python.
`SMOKE_GROUPS` selects fixed representative tests by required extension and
checks their names against upstream inventories. The target description is
part of the build cache key. Validate every selected ELF's physical PT_LOAD
ranges (using `p_memsz`, not file size) and executable entry before publishing
a manifest, including on cache reuse. Physical ISA tests have no dynamic
stack; adding C workloads requires an explicit stack/linker contract.

The simulation CI job reuses its MiniSoC and TiledSoC executables for
`isa-smoke`, attempts both targets even if one fails, and uploads independent
results. Changes to the adapter or upstream ISA sources must select that job.

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
make -C sims uart-pty-test SOC=simple
make -C sims uart-pty-test SOC=mini
make -C sims uart-pty-test SOC=tiled
```

FESVR's write-data wrapper retains lane placement, masks, and packet-position
policy while using [`chi/protocol/messages.rhdl`](../chi/protocol/messages.rhdl) for immutable
`NonCopyBackWriteData` construction. Preserve its explicit DataID, CCID, and
DBID/MECID choices independently of the core requester profile.

FESVR REQ construction is also immutable, with the same inactive-field zeros.
Keep opcode selection, cacheable/device attributes, address/size, and NodeID
conversion in this requester rather than sharing core policy. The `fesvr-mmio`
bench compares complete emitted requests and holds them through backpressure.

The transport checks require the pinned FESVR library; DPI checks also require
Verilator. Lowering requires the pinned CIRCT tool or an explicit `CIRCT_OPT`,
and execution requires FESVR plus the RISC-V cross compiler. Rhombus checks use repository wrappers with fresh
compiled roots. Technology-mapped simulation remains owned by
[`../vlsi/sim/`](../vlsi/sim/README.md).

`uart-pty-test` starts the ordinary simulator, discovers the production PTY path,
and exchanges all 256 byte values with a polling UART payload. Each byte is
checked by both the target and the external Python client, with transformed
replies, a final acknowledgement before process exit, and bounded cycle/wall
timeouts. It exercises the actual core, CHI MMIO, UART FIFOs, serial engines,
and PTY; no test-only DPI transport bypasses that path. Simulation CI runs it
on all three SoCs. Keep the UART C++ source/header in both ordinary and mapped
simulator link prerequisites when changing this shared harness dependency.

`make -C sims tiled-memory-test` builds a separate TiledSoC harness specialization
with independent REQ, RSP, write-DAT, and read-DAT stalls. Its target payload
dirty-evicts and refills a 64 KiB footprint across every LLC slice, checks the
last architectural memory line, and exits through FESVR. Harness assertions
require actual backpressure on all four flows and reads/writebacks from every
Home at the one external channel. Keep its build root separate from ordinary
simulators and never reuse an unstalled prebuilt binary for this target.

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

`zihintntl-test` runs `tests/programs/rv5stage_zihintntl.S` through the
checked-in SimpleSoC harness only. The payload's conflict bank is intentionally
matched to SimpleSoC's 64-set, four-way L1D profile; changing that profile
requires revisiting this test rather than silently reusing it for another SoC.
It calibrates warm and cold accesses on the running system, then compares minima
over repeated trials to tolerate unrelated HTIF snoops. The second hinted read
must remain a miss, and an intervening dirty line must retain its authoritative
value. The inclusive outer cache may invalidate that L1 copy while allocating
the hinted line, so this SoC test does not require the resident probe to hit;
the L1 no-replacement property is covered by
`tests/backend/verilog/rv5stage-dcache_tb.sv`. Do not weaken the NTL check to
data-only checks: ignoring NTL preserves architectural values and would
otherwise pass. The payload selects S-mode Sv39 data
translation through MPRV while executing in M-mode; test addresses are virtual
aliases outside the physical RAM window, so bypassing translation cannot pass.
It needs no supervisor runtime. Compressed and FP subcases are selected from
`misa`. MiniSoC and TiledSoC may enable Zihintntl but are not covered by this
geometry-specific end-to-end target.
