<!-- Guides contributors through maintaining executable SoC harnesses and simulator bindings. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing simulation harnesses

Read the simulator [README](README.md) for selecting, building, and running a
harness and for the public execution boundary. This guide owns implementation
placement, build artifacts, bindings, and contributor validation.

## Architecture and ownership

Simulation consumes a [`socs/`](../socs/README.md) composition and supplies a
parameterless top, coherent FESVR requester, optional
simulation-only memory, clock/reset driver, Verilator binding, and target
execution. Keep processor, device, CHI, NoC, and synthesizable-memory policy in
their owning packages. Keep DPI and target-loader behavior out of SoCs.

RV5Stage configs contain synthesizable harts. Every Spike config contains
the simulation-only `SpikeCore` DPI boundary while retaining the selected
SoC shape's coherent fabric and platform contract.

RV2Wide is also synthesizable and binds the existing Mini/Simple shapes with
`rv64imacb` and `rv64imafdcb`. Its four enrolled configs run only capability-filtered ISA smoke,
with ordinary BootROM/FESVR loading and target-selected Sail cosimulation.
Their optional flow tracing uses the same event pass and RHEG exporter. The SoC resolver owns
their fixed ISA and cache resources, not this harness.

Import the CHI owners used by each simulator component directly. FESVR consumes
wire, channel, service, and message contracts; the shared single-core harness
explicitly imports `chi/subordinate/memory-controller.rhdl` and
`chi/subordinate/dpi-memory.rhdl`. Neither needs the
all-CHI facade. The [CHI import guide](../chi/README.md#package-boundary-and-import)
owns the public entry-point contract.

Each `(SOC, CORE, ISA)` selection has an isolated build directory; direct SV
adds a `-verilog` suffix and SV-hosted rsim adds `-rsim`, so backend artifacts
cannot be reused accidentally.
CI names its enrolled configs explicitly and publishes an exact-commit simulator and target
descriptor for each selected build variant. The additional
`simple-rv5stage-rva23-verilog` variant reuses its architectural config and runs
only `smoke`, `host-mmio-test`, and `uart-pty-test`. The
`simple-rv5stage-rva23-rsim` variant reuses the same config and runs only `smoke`.
Software-only changes build
the six full-suite Single configs; simulation changes build all enrolled configs,
including all four Mini RV32 bindings, plus the direct-SV and rsim variants. The host emitter
selects a hart binding and specializes one of three shape-owned harnesses;
test-only module paths remain available for focused fixtures. Every selection
emits the same `SoCHarness` top contract. Preserve config-keyed artifact and
target identities so switching any axis cannot reuse another simulator.

The configured host emitter calls `elaborate` once and passes the resulting
program, including retained providers and its explicit top, to `compile_program`.
Backend targets own preparation; the SV-bound rsim target can retain native
constructs before generating its wrapper. Selected trace/co-sim RTL passes
explicitly require concrete preparation. Do not prepare the configured source
before target selection or reconstruct a program from only its design/top.

All enrolled Mini/Simple RV5Stage and RV2Wide CIRCT rows build with `COSIM=1`,
as does the RV5Stage direct SystemVerilog variant. Each instrumented row publishes one
simulator under its unchanged config artifact name; its local build directory
retains the `-cosim` suffix. The experimental Rsim backend stays uninstrumented.
The shared Sail job publishes the model and config-independent cosim archives;
simulator builds verify these archives and compile only their config-specific
session binding. Execution consumers need only native runtime dependencies and verify the
expected cosim variant and run the same shape/ISA workload selection, requiring a
successful nonempty checked stream. Unsupported checking paths fail explicitly;
CI enrollment is not a claim of complete checker support. Keep this choice in `tools/ci/policy.py`,
not a second config inventory or a separate cosim build/test loop.

The host selection layer now uses `socs/configs/resolve.rhm` for hardware,
software target descriptions, and UDB. `emit-soc-harness.rhm` and
`program-test/write-target.rhm` require an explicit third ISA selector (the
target writer also accepts a complete config key). `config.mk` delegates Make selector validation to the shared dependency-light
`socs/configs/selection.py` and gives hardware, software, and attestations the same canonical
shape-core-ISA identity. There is no ISA default. Spike runtime configuration
includes exact vector geometry, and its ACT/UDB projection preserves its own
architectural choices. `test-configs.txt` is the sole explicit
config inventory, consumed by CI and the typed `test-configs.rhm` view, separate from implementation support and workload
policy. Its focused contract test runs with the SoC host lane. CI callers now
use complete keys without expanding the workload inventory. ACT configuration
must match the selected config. Config-independent
setup and host adapter tests remain usable without an ISA selection.

Config metadata is projected by `socs/configs/metadata.rhm`, not reconstructed
in Python. The target JSON embeds that snapshot and its SHA-256; the harness emitter
writes the same fingerprint into emitted MLIR or SV. `simulator` records an attestation only after matching
those identities, and includes the selected backend and harness variant. The
emitter writes `rhodium-rtl-backend` provenance, checked during recording; every
prebuilt use verifies the requested backend against the sidecar. Missing backend
provenance requires rebuilding the artifact. Recording requires
the emitted target artifact; verification requires only the binary, attestation, and adjacent
target descriptor. ACT's generated UDB and payload archive carry the same configuration;
execution rejects a mismatch before running a shard. Keep these descriptors in
artifact uploads and cache identities, never as checked-in generated catalogs.

`RTL_BACKEND` selects `circt`, `verilog`, or experimental `rsim`, with CIRCT
remaining the default.
The harness emitter loads only the selected compile target and gives it the
same `ElaboratedProgram` and explicit top. Direct builds emit `SoCHarness.sv`
without an intermediate MLIR target; `SOC_EMITTED` routes the actual source
artifact to attestation. Keep compiler/backend imports in the host emitter,
never in circuit definitions. CI uses `simulator_id` for artifact identity and base build paths (instrumentation may add a local suffix),
`soc` for the unchanged hardware/software target, and `backend` to select
backend-specific tests. Backend variants stay out of architectural
config, ACT, benchmark, and OpenSBI inventories. Direct event tracing is not
supported.
Direct RTL can retain unsigned comparisons against zero and type bounds after
parameter specialization. Permit the additional `UNSIGNED` and `CMPCONST`
Verilator warnings for that route, retaining the existing simulator lint policy
and runtime assertions.

The experimental `RTL_BACKEND=rsim` route selects `rsim_sv_target` without
changing harness elaboration. `emit-soc-harness.rhm --backend rsim` requires
`--output-directory <dir>` for `SoCHarness.hpp`, `SoCHarness.cpp`,
`SoCHarness_bridge.cpp`, and optional `rsim-bits.hpp`; stdout remains the SV
wrapper with config provenance. The Make rule passes both generated C++
sources to Verilator alongside the unchanged `TestDriver.v` and native adapters.
Missing required model/bridge companions trigger a fresh emission. Tracing
remains CIRCT-only. CI enrolls rsim only as a bounded smoke variant.

Keep build wiring validation separate from execution qualification. Simulator
adapter tests cover backend directories, source arguments, and provenance.
The backend's [CHI differential fixture](../rhodium/backend/DEVELOPING.md)
qualifies one real native memory. Simple RV5Stage RVA23 also passes the existing
smoke ELF through the real FESVR loader and unchanged `TestDriver.v` with
`OPT_FAST=-O2` and `+max-cycles=100000`. For rsim helper/layout changes, qualify
optimized native model compilation separately, then link the matching model and
bridge into the unchanged driver and run that same ELF. Keep native compile time,
Racket elaboration/emission time, and simulator runtime separate. Compare identical
compiler flags when attributing a change to partitioning; a comparison between an
optimized region model and an unoptimized monolithic model includes both effects.
The [simulator guide](README.md#select-rtl-emission) records qualified settings.
Use a fresh build root or object directory when changing optimization flags,
since object timestamps do not encode those flags. Broader workloads and other
compiler/optimization configurations require separate execution validation.
Local builds and CI select `OPT_FAST=-O2` for every backend. `OPT_SLOW` and
`OPT_GLOBAL` inherit `OPT_FAST` unless explicitly overridden, matching optimization
across model, initialization, adapter, and Verilator runtime compilation. Pass
all three settings explicitly to Verilator's generated Makefile, which otherwise
sets its own defaults. Preserve command-line overrides, including flags with spaces.

`tools/ci/policy.py` owns the shared simulator `opt_fast` setting and backend smoke
variants, including rsim's `smoke_max_cycles` and `harness_timeout_minutes`.
The simulator workflow passes the optimization override explicitly to Make; the harness workflow uses the
attested prebuilt binary, bounds execution, and uploads per-target logs even on
failure. Keep these variants outside `test-configs.txt` and the ACT, benchmark,
and platform-test inventories. Test changes with
`python3 -m unittest tools.ci.test_plan` and `make -C sims program-test-adapter-test`, then rebuild
and run the selected smoke through the prebuilt-artifact path. Linux CI remains
the owner of runner-specific compiler, memory, and timeout validation.

## Implementation map

| Concern | Owner |
|---|---|
| Build graph, tools, variants, and artifacts | [`Makefile`](Makefile) |
| Required ISA selection and canonical Make artifact identity | [`config.mk`](config.mk) |
| Shared dynamic harness emitter | [`emit-soc-harness.rhm`](emit-soc-harness.rhm) |
| Shared external-memory single-core execution harness | [`single-core-soc-harness.rhdl`](single-core-soc-harness.rhdl) |
| Distinct internal-RAM and tiled harness circuits | [`mini-soc-harness.rhdl`](mini-soc-harness.rhdl), [`tiled-soc-harness.rhdl`](tiled-soc-harness.rhdl) |
| Direct-memory FESVR transport and CHI requester | [`fesvr/`](fesvr/) |
| Pinned upstream FESVR and shared downstream patches | [`../riscv/riscv-isa-sim/`](../riscv/riscv-isa-sim/), [`../riscv/riscv-isa-sim-patches/`](../riscv/riscv-isa-sim-patches/) |
| Verilator VPI/DPI binding | [`verilator/`](verilator/) |
| Clock, reset, and exit | [`TestDriver.v`](TestDriver.v) |
| Generic native lifecycle, instrumentation options, and cycle barriers | [`verilator/simulation_runtime.cc`](verilator/simulation_runtime.cc) |
| PTY transport and serial conversion reused by every harness | [`../devices/uart/uart-dpi.rhdl`](../devices/uart/uart-dpi.rhdl), [`../devices/uart/dpi/uart_dpi.cc`](../devices/uart/dpi/uart_dpi.cc) |
| Harness checks and smoke payload | [`tests/`](tests/) |
| Simulator `SOC` name to canonical architectural description | [`program-test/targets.rhm`](program-test/targets.rhm) |
| Target software sources, ports, patches, and ELF builders | [`../sw/`](../sw/DEVELOPING.md) |
| SoC target generation, workload execution, and simulator artifacts | [`program-test/`](program-test/) |
| Bare-metal litmus ELF generation and pinned model states | [`../sw/`](../sw/DEVELOPING.md) |
| Embedded Sail library boundary and focused host validation | [`cosim/`](cosim/README.md) |
| Shared UDB-to-Sail architectural projection and config identity | [`sail/`](sail/README.md) |
| ACT platform configuration and execution adapter | [`arch-test/`](arch-test/) |
| OpenSBI DTB projection, simulator handoff, and validation | [`opensbi/`](opensbi/DEVELOPING.md) |
| CHI simulation memory | [`../chi/subordinate/dpi-memory.rhdl`](../chi/subordinate/dpi-memory.rhdl) and [`../chi/subordinate/dpi/`](../chi/subordinate/dpi/) |

## Embedded Sail reference

[`cosim/`](cosim/README.md) owns target-selected observation, ordered event
reconstruction, and independent Sail checking. Its [contributor guide](cosim/DEVELOPING.md)
owns the source map, dependencies, callback/sample invariants, and focused tests.
[`sail/`](sail/README.md) owns the shared ACT/cosim architectural projection and
embedded Sail package metadata.

`COSIM=1` selects the compile pass and native runtime without changing the HDL
generator or exact config profile. The generic Verilator runtime supplies sample
barriers, successful FESVR writes, and bounded exit draining. Keep instruction
coverage in the existing shape/ISA software suites; do not add a parallel cosim
qualification matrix.

## Add or change a harness

Follow the repository's [source documentation requirements](../AGENTS.md#source-documentation),
including the exemption for files under `tests/`.

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
   shared. Select a single-core binding in the host emitter or map the new
   `SOC` value to a harness module. Load only the selected system.
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

Mini/Simple RV2Wide use the same event target, including composition with cosim.
`trace-smoke` reuses the scalar cosim payload and `check-rv2wide-events.sql` checks
fetch/packet-to-issue ownership, dual-slot issue, PC/instruction preservation,
fixed EX/MEM timing, successful WB, exact EX+5 multiply writes, and the
three-stage variable-return path. The existing RV5Stage trace checks remain separate because its stage and
prediction contracts differ. Keep trace selection out of SoC/ISA/CI inventories.

`FesvrRequester` leaves the injected DPI command boundary unannotated; production
host traffic adds no visible checkpoints. `FesvrCHIAccess` declares one retained command
scope covering CHI REQ fragments, write DAT, and the host response. Capture at
command acceptance and release only when the host accepts completion, including
errors; intermediate fragments and DBID responses must not release the owner.
Snoop handling remains independent from host-command ownership. The
`event-fesvr` backend fixture supplies its own command root and output checkpoints,
reuses the ordinary MMIO regression, and checks
exact occurrence parents through fragmented reads/writes, errors, stalls, and
pending reset. Its service configuration lives in `tests/fesvr-mmio-fixture.rhdl`.

The shared single-core harness owns transparent external-memory checkpoints. Keep them
outside synthesizable SoC code. `emit-soc-harness.rhm` selects the same SoC
elaboration for ordinary and event-instrumented builds. Its selected target
composes `event_trace_pass` with the RTL backend. `--trace OUTPUT_DIRECTORY`
saves the compilation's `events.json` and `events.h` as `soc_events.json` and
`soc_events.h` alongside MLIR, adding the simulator's configured frequency to
the header. Both descriptors come from the same compilation as the RTL; no
separate descriptor export or second elaboration is used. The opt-in build links `verilator/event_trace.cc`
with the independent RHEG libraries. Do not duplicate collector or encoder
logic in this adapter. The adapter selects gzip only for a `.gz` output suffix and calls
the exporter's checked `finish()` before closing the file on exit or timeout.
`RHEG_PERFETTO_TRACKS` optionally names an explicit shared-track configuration.
Parse it with the common exporter API and pass it unchanged to the writer;
do not duplicate membership, capture-schema, or collision logic in the binding.
`TestDriver.v` releases reset and observes completion on
falling edges; trace batches therefore follow all rising-edge callbacks.
Bind descriptor and timing before callbacks, and flush the final settled cycle
before normal exit or timeout. Keep emitter, generated clock constant, descriptor,
and RTL tied to the same harness configuration.
The emitter opts into `EventInstrumentationConfig(~partial: #true)` to preserve
supported ancestry when other branches lack contracts. Keep manifest gaps and
runtime unknown-ancestry markers intact; do not suppress opaque branches merely
to make instrumentation succeed. Partial mode does not waive contract validation.
Verilator's generated link rule omits user archives from its prerequisites.
When the outer simulator target is stale, `verilator/relink.mk` marks only the
generated executable target phony to force linking. Its model archive still
follows normal dependencies on both initial and incremental builds. Check both
an absent model archive and an archive-only library update when changing this rule.

Run the real smoke with native importer validation:

```sh
make -C sims trace-smoke SOC=simple-rv5stage-rva23 TRACE_FILE=/tmp/single-core-rv5stage-soc.pftrace \
  TRACE_PROCESSOR=/path/to/native/trace_processor_shell
```

`tests/check-event-trace.sh` requires request and returned-data traffic and checks
the external-memory channel schemas and opcode labels, exact configured
timestamps, one-cycle transfers, continuous stall ranges, readable track labels,
and importer errors.
Refill, writeback, and walk residencies are checked separately from transfer/stall
tracks; a settled trace prefix may retain an unfinished open residency.
It loads `tests/event-tracks.sql` first: the `rheg_tracks` view exposes full
annotation labels (or explicit shared-track labels) as `name` and local display names as `leaf_name`. Load that
preamble before running an individual `check-*-events.sql` query too. Groups have
no event schema and are excluded from this view. Check visual parent chains and
ordering against native `track`, not by assuming every event is a root child.
It checks run timing once in the metadata table and site/capture context in
track descriptions, with no redundant run metadata on occurrences. SingleCoreRV5StageSoC
instruction and CHI transfer names cannot equal `stall`, so these checks use
that slice name to separate stalls on shared tracks. Do not require exact graph
identity reconstruction from the Perfetto visualization.
This optional test requires native Perfetto
and does not require RSP activity: the smoke's reads return data on DAT.
It is separate from ordinary simulation CI. `tests/check-event-driver.sh`
checks missing trace path, failed output open, and a small-cycle timeout with
an importable settled prefix. Also run untraced SingleCoreRV5StageSoC smoke after changing
the common driver.

`tests/check-core-events.sql` additionally requires Decode through WB,
exact permitted core-stage edge families, one MEM parent per WB event even
for memory instructions, one parent per EX/MEM event, no duplicate
scalar children, matching RV64 PCs, and one-cycle downstream latency before WB.
WB is successful retirement: retained WRS/CMO instructions may retire later,
and replay/trap attempts do not emit it. Its two-bit `branch_prediction` capture
uses the core's enum table and displays its symbol in Perfetto; it compares
effective predicted and resolved next PC. Enum arguments use symbols throughout
the exporter, so importer queries compare names rather than packed numbers.
The independent one-bit `ras_mismatch` compares predicted and resolved stack actions.
Decode
inherits packet ancestry; the raw packet boundary is not an IF/ID transfer. It requires
repeated decoded PCs to exercise distinct occurrences.
PC and instruction checks use named captures, independently of core bundle layout.
`check-frontend-events.sql` connects request, lookup, outcome, and Decode
tracks, including selected fetch causes: preceding S0 successors, S2 replays
or registered prediction repairs, and core redirects/retries. A selected retry
may inherit MEM and its paired LSU-result occurrence; other causes have one
selected parent. Only requests reached through an unmodeled cause carry
unknown ancestry. The direct `rv5stage-fetch-source` fixture checks exact
selection and held-cursor ownership against public controls, including blocked
restarts and replacement. Decode and its stalls have one or two retained/live, admitted S2
parents; they are no longer roots. S2 captures admission and fault flags in the
single outcome event, and empty-queue bypass permits same-cycle instruction
consumption. The cycle-level `event-frontend` fixture owns exact compressed/straddle
parent reconstruction; the smoke checks importer-visible edge families.
Select stages through track names, not mnemonic slice names, and check full
disassembly separately from the mnemonic. Generic display/schema rules belong
to [RHEG](../rheg/DEVELOPING.md#perfetto-encoding), not this adapter.
`tests/check-memory-events.sql` checks one transfer site per external-memory
channel, named metadata captures, opcode slice names, and memory-relative DAT
direction. Idle RSP/RXDAT channels need not appear. The harness emits no duplicate
wire-end checkpoints and no raw data payloads. Missing ancestry remains explicit;
do not match reused CHI IDs in the exporter to synthesize request/response edges.
Every memory REQ and incoming write DAT must have one Home-transaction parent,
including traffic from a host-originated Home transaction. The Home checkpoint
is known even when its own earlier ancestry is unknown.
The D-cache stage checks in `tests/check-demand-events.sql` follow scalar EX
into shared S1 resolution one cycle later, then the returned cache occurrence
into WB alongside its independent MEM parent. The caller-owned
`dcache/s2.resp` is a sibling of retirement, retaining the same MEM and cache
occurrences without depending on whether WB fires. Successful ordinary scalar
memory results have same-cycle WB siblings; rejected/faulting attempts have none.
Admitted results parent S3. They also check one-cycle S3-to-S4
advancement and S4 refill acceptance fields. Keep caller effective addresses
separate from physical cache addresses; translation need not preserve their
numeric value. Direct S4 refill
acceptance reaches refill residency on the same graph cycle; Perfetto displays
the registered residency beginning one cycle later. TXREQ follows with matching
opcode/line address; retries may produce multiple children of one residency.
Writeback residency separately parents its requests, data, and post-eviction
refill. Its incoming gather ancestry remains explicitly unknown.
For a vector-memory workload such as vec-daxpy, additionally load
`tests/check-vector-memory-events.sql` after the track preamble. It requires
every vector issue to have exactly one S1 sequence parent one cycle earlier,
preserving the beat index and packed/elementwise geometry. It also requires
scalar and vector traffic on one cache-resolution site, vector load/store hits,
minimum issue-to-cache timing (retained certified requests may retry later),
one-cycle return capture, identical issue occurrence
parents on both paths, and admitted vector continuation into S3. It is not a
requirement on the scalar-only smoke or workloads without vector memory.
Restrict those pipeline checks to transfer sites. `tests/check-stall-events.sql`
requires real Decode backpressure, matching capture layouts, the exact
Boolean hazard fields, and admitted S2 parents for Decode stalls.
Surviving instructions follow their own stalls; match both instruction PC and
parent occurrence because two compressed instructions can share a word parent.
It rejects outgoing edges from any stall observation. Keep these checks separate
from transfer fanout and fixed-latency rules. Check durations, non-overlap, and
captured reasons on each coalesced stall slice.

`tests/check-cache-events.sql` checks observed private-cache CHI descriptors
(the importer omits idle channels from its track table),
exact named capture layouts, actual I/D request and refill-data activity, node
identity, decoded opcode slice names against per-channel enum metadata, successful
response status, byte-addressed snoops, and certified event
lineage at transaction boundaries. The demand-only smoke requires every I-cache
TXREQ to have one refill parent with one S0 predecessor and at least three cycles
of total delay; the
integrated fetch fixture checks exact ownership independently of the exporter.
`check-home-events.sql` checks both caches' request-to-return edges and requires
unknown markers where an opaque branch lacks a parent. It also requires one
known RXDAT parent per CompAck, with matching DBID and requester identity,
positive latency, and no later RXDAT before the acknowledgement. The engine
fixture owns independent packet-set and reset checks, including ROM reads.
Keep scalar and external-memory
checks scoped to their own tracks when adding cache channels. See the
[RV5Stage annotation owner](../cores/rv5stage/DEVELOPING.md#pipeline-event-annotations).
Validate cache stall slices on the original channel tracks, always named `stall`,
with the same per-channel captures and opcode tables as transfers; never require
a quiet channel to stall. RN-I
instruction requests use nonsnooping `ReadOnce` snapshots; preserve that
transaction distinction when checking expected traffic.

### Other simulation contracts

The ACT flow is included from `arch-test/Makefile.inc`.
`arch-test/write-platform.rhm` resolves the selected config once and writes its
implementation-specific UDB alongside generated `platform.json`.
`arch-test/platform.rhm` derives the payload RAM window and entry point from
the SoC description and finds a page-sized hole outside its described RAM,
BootROM, boot register, and devices. `configure.py` validates that the hole
is large enough for scalar and vector tests, fits the physical address width,
and does not overlap a Sail memory region before publishing
`RVMODEL_ACCESS_FAULT_ADDRESS`. No per-core or per-ISA Make configuration is
authored. ACT currently accepts the Simple shape; adding another shape requires
validating its platform macros and memory capacity, not adding a config table.
Processor extension policy stays in the owning core's UDB projection. The common
`configure.py` writes generated UDB consumer files, using `sail/` for the pinned
default schema and explicit UDB mappings. Reject unsupported architecture
shapes before producing reference results. Always give ACT the full test
inventory and delegate extension closure and test constraints to it. Do not
maintain a separate suite selector. Replace only the configuration's generated
ELF outputs before each build so changes to core support cannot leave stale
tests for the upstream runner; preserve cached reference intermediates.
Reference/harness limitations remain distinct from core extension support;
extend and validate the projection as newly selected suites expose gaps.

`arch-test-source` copies the clean upstream checkout into the build root and
applies the ordered series under
[`../sw/riscv-arch-test-patches/`](../sw/riscv-arch-test-patches/) with
the shared RISC-V patched-submodule materializer.
`arch-test-sail-setup` uses the same materializer for the pristine
[`../riscv/sail-riscv/`](../riscv/sail-riscv/) gitlink and its
[`patch series`](../riscv/sail-riscv-patches/series). The Sail 0.14.1 emulator
is built locally with compiler 0.20.3 and installed under an identity-keyed
`.tools/` path. Linux downloads that compiler from a checksum-pinned release;
macOS requires an explicit local compiler. The model build never edits the
submodule, and changing its gitlink or patch bytes changes the emulator path.
The pinned master revision still reports release version 0.14.1; cache and
package identity must use the gitlink, ordered patches, and compiler version,
not the emulator's release string alone.
`arch-test-tests` copies the handwritten inventory from that materialized tree
and populates it with the canonical `testgen` command. Vector assembly is not
checked into the upstream test tree, so it must be generated through this same
path instead of a separate generator or manual prerequisite. Keep the pinned
submodule pristine; the patch series, not a dirty submodule checkout, is the
reviewable source of downstream changes.
ACT guards optional `mcountinhibit` accesses and emits dedicated inhibit tests
only for writable bits advertised by `COUNTINHIBIT_EN`. The shared Sail
projection supplies the exact writable mask for generic CSR comparisons.
The Sail adapter
projects the independent UDB AMO and LR/SC misalignment choices to their
corresponding exceptions without changing the DUT claims. The ACT-local UDB
overlay declares `AMO_MISALIGNED_BEHAVIOR`, which is absent from UDB 0.1.17;
both core projections state it explicitly. Do not infer AMO fault selection
from scalar misalignment support or the LR/SC policy.

Always enable privileged tests as well; missing platform hooks and reference
model mismatches must surface as build or execution failures, not suite exclusions.
Use ACT's keep-going mode to attempt every selected build even when others fail;
the generation command still returns failure if any build fails.

Project enabled vector profiles into Sail's native vector configuration from
the UDB `VLEN`, `ELEN`, reserved-vtype behavior, and VS-dirty contract. Validate
the implied Zve and cumulative Zvl closure before generating results, and keep
optional vector extensions as ordinary Sail feature switches. Profile closure
belongs to the owning RISC-V/core generators; the ACT adapter must diagnose a
missing implication rather than silently synthesize one.

`arch-test/build.py` invokes the upstream CLI without replacing its required
Sail version. Keep the setup pin synchronized with that native check whenever
the ACT submodule advances. The Sail 0.14.1 projection also uses the
optional LR/SC exception encoding and clears H-only delegation bits when H is
disabled in UDB.
Upstream Sail gates writes through both `mie.SGEIE` and its `hie.SGEIE` alias
when GEILEN is zero, matching the forced-zero `mideleg.SGEIP` bit. Keep the
positive GEILEN case writable. `arch-test-sail-test` compiles a small CSR probe
and checks both configurations against the patched model; the RVA23 ACT shards
then exercise generated reference signatures against the DUT.
The shared projection now maps `MCOUNTINHIBIT_IMPLEMENTED` and the exact
`COUNTINHIBIT_EN` mask to Sail's native configuration; these are no longer
reference-model differences. Keep HPM event-counting differences explicit.

For H profiles, also project guest translation modes, VMID width, GEILEN,
H counter enables, VS trap-vector modes, guest-fault reporting, and the nested
Smstateen/Ssstateen switches. VS vector status exists exactly when H does.
Map the supported always-zero `TINST_VALUE_ON_*` claims explicitly to the
eight transformed-instruction switches; do not inherit Sail's enabled defaults.
New optional extension defaults are disabled unless the hart's UDB advertises them.
The shared RVA23 preset uses this path for both RV5Stage and Spike. Enabling
the profile does not create ACT coverage: report missing H/Sha test inventory
separately from generation failures and runtime results. Do not replace it
with a scalar or non-H reference profile to make a lane pass.
Pinned UDB 0.1.17 natively accepts legal zero-GEILEN harts; no lower-bound
overlay is required. The ACT-local UDB copy references the supported
`arch-test/udb-overlay/` mechanism for remaining schema corrections; its
extension list and parameter values are unchanged.
The overlay removes UDB's erroneous Sstvala and Shvstvala requirements
that EBREAK report its PC: the profiles explicitly exempt EBREAK/C.EBREAK.
UDB has only one breakpoint-reporting flag, so this narrower exception cannot
be expressed by that flag. All other trap-value requirements remain enforced,
and DUT reporting choices stay intact.

`Za64rs` and `Za128rs` are reservation bounds, not Sail extension switches.
Validate their versions and bounds against Sail's naturally aligned reservation
size without enlarging it. The pinned default is eight bytes: a conforming
reference choice, not an assertion that it exactly reproduces the DUT's
access-sized LR.W reservation. `Zic64b` projects and checks a 64-byte cache block
even when CBO instruction extensions are disabled. Keep these cases in the
adapter tests so profile guarantees cannot silently bypass platform validation.

Generated YAML, Sail JSON, linker scripts, headers, ELFs, patched ACT source,
and logs stay in the ACT build root. Keep downstream canonical-generator changes
as ordered parent-repository patches until they land upstream. When updating
the pinned upstream revision, remove landed changes, rebase the remaining
series, and check the required Sail version, bundled UDB gems, generator output,
and header/runner contracts together. The shared linker layout keeps test data
addresses equal between Sail signature payloads and self-checking DUT payloads;
model-specific text and HTIF mailboxes follow test data and stack.

At ACT pin `fa1debda686aa035dfb7bcd5c333c86dafcd1004`, native vector-FP
generation supplies operand widths and separate scalar/broadcast register
groups. Native privileged generators use record-counted trap signature storage,
UDB counter guards, and bounded LR/SC retries. Do not restore the retired
replacement FP generators, word-count sizing, or zeroed LR/SC result patches.
The seven remaining patches add canonical crypto/bitmanip expansion, legal
whole-register vill setup, ordered-index alignment, base delegation coverage,
and direct S/U timer programming.

Run `make -C sims arch-test-adapter-test program-test-adapter-test` for generation,
completion, deadlines, artifact identity, and complete-result checks using system
Python without ACT dependencies. Run `make -C sims litmus-setup litmus-adapter-test`
separately for tests requiring the pinned source corpus. CI runs the shared
checks before workloads and the litmus check only after its source setup. With ACT
installed, run `make -C sims arch-test SOC=simple-rv5stage-rva23` to generate and execute all applicable
tests; the architecture-test CI lane uses this same target. When changing the driver, also run the existing smoke
and exercise a small `+max-cycles` timeout. See the
[operator guide](README.md#architectural-certification-tests) for setup and
current coverage limits.

`sims/tests/config-test.rhm` checks ACT platform equivalence across core
bindings and relocation through the SoC description. The ACT writer is in the
root Racket compilation manifest so configuration generation can reuse CI bytecode.

### LR/SC system validation

`make -C sims lrsc-test SOC=simple-rv5stage-rva23`, `SOC=mini-rv5stage-rva23`, and `SOC=tiled-rv5stage-rva23` complement the
[full-core progress matrix](../cores/rv5stage/DEVELOPING.md#ziccrse-progress-gate).
`tests/programs/lrsc.S` runs six constrained-loop placements, covering
LR.W/SC.W and LR.D/SC.D at aligned, cross-line/page, and page-boundary starts. Each
loop contains sixteen contiguous instructions including its retry branch.
Setup, barriers, function returns, signatures, and HTIF exit are outside the
constrained loop. The linker keeps code, shared counters, and page tables
separate. Bare and supervisor Sv39 executables use the same identity-mapped
RAM, with independent 4-KiB leaf mappings and preset A/D bits. MiniRV5StageSoC places
its page tables inside its 64-KiB RAM; all RVA23 configs use halfword boundary
starts. The payload checks `misa.C`
against the selected alignment, and the linker rejects out-of-RAM placement.

Every SoC uses its ordinary harness and production ROM. The tiled target adds
`+boot-harts=0-7`; the single-core targets use the default hart-zero selection.
Every selected hart waits for the normal FESVR post-loading entry publication
and its own ACLINT wakeup. Do not replace the
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

Keep these builds isolated under `BUILD_ROOT/lrsc-test/`. Both Bare and Sv39 executions must be
attempted, with a nonzero overall status if either fails. Preserve failed
results and do not count timeout, post-pressure recovery, or partial
signatures as validation success. This finite regression is evidence for
the concrete configurations, not an advertisement switch or a proof for
arbitrary external fabric fairness.

### Software suite and artifact maintenance

[`../sw/build/isa.mk`](../sw/build/isa.mk) includes upstream build rules and selects their physical
and virtual-environment test inventories. `program-test/write-target.rhm` projects the concrete SoC
profile through the pure RISC-V GNU adapter, while [`../sw/build/build.py`](../sw/build/build.py) owns benchmark
selection, mode choice, compiler probing, ELF-attribute checks, and
content-addressed ELF directories. [`../sw/build/build-coremark.py`](../sw/build/build-coremark.py) separately compiles the
pristine CoreMark submodule with the Rhodium-owned RV64 port under
[`../sw/coremark-riscv-baremetal/`](../sw/coremark-riscv-baremetal/). Its `coremark_scalar` variant disables
GCC auto-vectorization without changing CoreMark's types or CRC requirements.
The variants have distinct manifests, cache keys, CI selection, and execution
artifacts; neither short functional run is a CoreMark score.
[`../sw/build/build-embench.py`](../sw/build/build-embench.py) likewise compiles the recorded upstream Embench-IoT
development revision with the RV64 port under
[`../sw/embench-iot-riscv-baremetal/`](../sw/embench-iot-riscv-baremetal/), checks the complete source-directory
inventory, materializes build-only copies whose local loop scale is explicit,
and publishes one target-bound ELF per workload. Its xgboost functional profile
must retain a bounded selection covering every class, a pinned exact-correct count,
and strict source markers so an upstream source change fails generation instead
of silently weakening the oracle. Record functional profiles in the manifest
and keep full upstream datasets in performance-oriented flows.
[`../sw/build/build-bringup-bench.py`](../sw/build/build-bringup-bench.py) compiles all names from the pinned upstream `BMARKS`
inventory by default. Its RV64 port is under
[`../sw/bringup-bench-riscv-baremetal/`](../sw/bringup-bench-riscv-baremetal/); the adapter builds a copy of
upstream's `libtarg.h` with a Rhodium target condition, leaving the submodule
pristine. It requires a checked-in upstream hash for each workload and embeds
that reference in the target-side output check unless a bounded source patch
requires a pinned replacement hash. The full inventory, selected names,
compiler/target identity, patch series, and port sources participate in the
cache key. There is one bounded functional suite for all upstream names; no
full-versus-smoke profile switch. Validate replacement hashes with an
independent host run and standalone Spike when changing a patch. A focused
`BRINGUP_BENCHMARKS` selection is for local diagnosis, never a failure-based exclusion. These
architecture-neutral benchmarks are not RISC-V package dependencies. Update
selections for architecture or
execution-environment compatibility or the routine CI time budget, never to hide
functional failures. Keep sources in
each pinned submodule untouched. Compiler/source/adapter changes must invalidate
binary reuse; regenerate the manifest on every build invocation.

Target-native benchmark builds are the default. `baseline` is an explicit
comparison mode, not an alternative hardware capability claim. Keep the target
descriptor and its fingerprint in the workload manifest, bind the same
fingerprint into the simulator attestation, and reject mismatches before
execution. Compiler acceptance and normalized ELF attributes prove that a
binary may use the target extensions; they do not prove dynamic instruction
coverage.

`program-test/write-target.rhm` projects the existing concrete SoC description
to the workload adapters, including its canonical implemented hart IDs; do not
duplicate ISA, topology, or RAM constants in Python. A manifest test may select
a nonempty canonical subset through `harts`. The runner validates that subset
against the target and translates it to `+boot-harts=`; tests without the field
retain the hart-zero default.
Both Single-core RVA23 configs own the complete profile-selected ISA inventory,
benchmarks, CoreMark, Embench-IoT, and Bringup-Bench. Both single-core SoCs own independent ACT configurations:
Spike's UDB projection reflects its pinned implementation, and RV5Stage uses
its own projection. Each Sail configuration and generated ELF inventory must
match the implementation under test.
All four Simple RV32 configs additionally select their full upstream ISA inventory
and ACT in CI. Program suite selection is keyed by shape/ISA in `tools/ci/policy.py`;
RV64 benchmark ports remain outside RV32 coverage. ACT projects XLEN, physical
addressability, indexed-memory EEW and vector geometry independently. RV32
configs use 32-bit physical addresses and disable PMP; both retain the platform's
44-bit CHI fabric. Generate each core's own Sail
expectations; never reuse one implementation's WARL/PMP claims for the other.
The six CI Mini configs and both Tiled configs use capability-filtered ISA smoke.
For each of `rv64max`, `rv64imacb`, and `rv64imafdcb`, only the paired Simple
configs are enrolled. These six configs run only `isa-smoke`: no platform suite, full
native ISA suite, ACT, benchmarks, OpenSBI, or litmus. Keep this policy keyed by
shape/ISA so RV5Stage and Spike exercise the same capability-filtered selection.
Both Tiled configs additionally own the focused upstream multihart benchmark selection. The
Make targets and CI select only eight participating harts, retaining all three
workloads without repeating them at smaller hart counts. The
adapter materializes a private build-tree view of the pinned benchmark sources
for each supported two-, four-, or eight-hart run, changes only the copied
`common/crt.S` hart-count constant and `common/syscalls.c` exit aggregation,
and records the selected boot subset in each
test's manifest entry while retaining the complete physical target for simulator
attestation. Success requires all selected harts to reach `exit`, while any
nonzero exit is reported as failure. Keep the source submodule pristine and
make both exact upstream markers fail closed when their runtimes change. This
coverage assignment is test policy, not hardware metadata; do not add a suite
category to an SoC or core configuration.
`ISA_GROUPS` maps target ISA extensions to XLEN-compatible upstream groups;
the pinned upstream's RV64-only CBO group is recorded as a coverage gap for RV32.
Full selection also
uses the projected MMU and privilege modes to include their virtual-environment
inventories when Sv39 and M/S/U are available. `SMOKE_TESTS` selects fixed
physical-environment representatives. Both modes
bind their manifests and simulator attestations to the generated target
description. The target's suite build specification and selection mode are part of the build
cache key; the full descriptor still binds execution to the exact simulator. Validate every selected ELF's physical PT_LOAD ranges (using
`p_memsz`, not file size) and executable entry before publishing a manifest,
including on cache reuse. Physical ISA tests have no dynamic stack; the
upstream virtual environment owns its own stack and page tables. Keep its
fixed `0x80000000` linker and DRAM assumptions compatible with each selected
SoC, or adapt those assumptions before adding another RAM layout.

One shared producer emits each selected target descriptor and required OpenSBI
DTB once. The target and device-tree writers accept `--batch` followed by
config/output pairs, loading shared dependencies once while retaining their
single-target CLI forms. Harness ISA smoke and eight-hart benchmark ELFs are built once per
compatible suite projection before the simulator matrix starts. Each simulator
producer publishes its exact-commit simulator and shared target descriptor before
its own harness starts; no harness waits for unrelated simulator builds.
`ci-harness.yml` verifies the simulator and binds shared ELFs to its exact target.
`isa-smoke-run` and `tiled-mt-benchmark-run` execute without a software builder;
their original build-and-run targets remain available locally. Standalone smoke,
MMIO, boot, and UART payloads still use the compiler. Missing prebuilt artifacts
are errors, never permission to regenerate them. Native DPI/transport,
cosim-hook, runtime, ACT/program adapter, and selected Spike native/ABI checks
run once in the shared native job.
Mapped Mini execution
retains its separate build dependencies; Spike consumers retain their runtime
library setup. Configs other than the three Simple smoke-only
presets run platform checks; Mini
and Tiled profiles run `isa-smoke`, and both Tiled cores run multihart
benchmarks. `tools/ci/policy.py` selects software targets by `(shape, ISA)` only;
each core binding consumes the identical selection. The matrix disables
fail-fast, attempts every selected target even after failure, and uploads independent results.
Changes to the adapter or upstream ISA sources must select that job.

Every simulator producer retains its build log and Verilator `--stats` reports
in a separate `simulator-build-<simulator_id>-<commit>` diagnostic artifact,
including failed builds. The reports expose conversion-pass timings separately
from native C++ compilation; use them before tuning output splitting or disabling
an optimization. Compare simulator execution on the same ELFs and configured
hart count before accepting a build-time improvement. Keep assertions and the
hardware configuration unchanged during these comparisons.

Program suite CI consumes the shared target descriptions, then
`tools/ci/programs.py` groups builds by the suite-specific
projection in `sw/build/program_target.py`. Groups use actual target fields,
not a shape/ISA naming assumption. The six program-suite Simple configs produce
eight build groups and sixteen execution jobs. Build jobs consume
`PREBUILT_PROGRAM_TARGET`, compile once per group, and publish checksum-bearing
archives; their caches are keyed by the group and software inputs rather than
the simulator config. `sw/build/bind.py` validates each archive against the
execution job's downloaded target and writes its separate run manifest.
`isa-run`, `benchmark-run`, `coremark-run`, `coremark_scalar-run`, `embench-run`,
and `bringup-run` run that manifest through the ordinary attested simulator
without invoking a builder. The corresponding `*-test` targets still build and
run for local use. Run jobs preserve all suite limits and workload coverage;
they attempt every successfully published group even if another build failed.
Program adapter contracts run once in the native job. Platform tests
use the same grouping/archive/binding machinery in `ci-platform.yml`:
`platform-plan` groups the shared targets and OpenSBI DTBs, `platform-build`
compiles each compatible group, and `platform` executes every config.
`litmus-smoke-run` and `opensbi-smoke-run` consume existing bound manifests;
neither depends on a builder. The OpenSBI test entry carries a checksum-bearing
auxiliary `payload`, passed through FESVR's existing `+payload` option; its DTB
is a checksum-bearing manifest file. Keep firmware relocation validation in the
software builder/binder rather than adding another ELF loader to the simulator.
ACT retains independent implementation-specific generation and expectations.
The ordinary `smoke` target specializes its payload by ISA: for RVA23, in addition
to boot and UART/PLIC behavior it executes vector loads/stores, checks VLEN=128,
exercises ELEN=64, and checks Zvbb plus double/half vector FP results. It uses
the same FESVR and prebuilt-simulator path in CI; no alternate loader or harness
is involved.
All RV32 bindings use the same payload's integer-vector branch with
VLEN64/ELEN32. RV32Max also selects FP32 coverage through `__riscv_zve32f`;
double and half coverage use their own compiler capability macros, not preset
names. `tests/programs/xlen.h` selects register-sized loads/stores for
platform payloads. The boot register and HTIF mailboxes remain eight bytes:
RV32 payload termination writes the low word of a zero-initialized mailbox, and the MMIO signature test
explicitly writes both boot-register word lanes. Keep the smoke's boot-register
readback coverage for both XLENs and run RV64 smoke after changing this shared
assembly. All four Mini RV32 bindings run this smoke in CI alongside
boot, host MMIO, UART PTY, and ISA smoke.

Simple RV32Int/RV32Max use these same width-selected platform payloads on both cores,
but the external-memory shape runs the full applicable upstream ISA inventory
with `isa-test`. Keep architectural selection identical while preserving
RV5Stage's pipelined multiplier and larger queues/caches. The core-independent
CI inventory includes all four configs. Their ISA and ACT lanes retain
the same shape/ISA selection policy; selecting a lane does not claim ACT validation.

The architectural `zihintntl-test` checks translated integer/FP hinted loads and
dirty-data preservation on both single-core implementations. The separately
named `zihintntl-policy-test` retains RV5Stage's concrete non-allocation timing
assertions and is a microarchitecture check, not a software-selection exception.
Supervisor payloads open PMP where present before using MPRV or S-mode; absent
PMP CSRs are handled by a local startup continuation, not core-name dispatch.
ACT retains exact ISA and implementation parameters, and emits
`reference-model-differences.json` for legal DUT choices not configurable in
Sail 0.14.1. These differences neither rewrite the DUT nor suppress test cases.
The opcode-free `Ssu64xl` declaration maps to Sail's fixed RV64 user execution,
not an extension switch. The adapter checks its version, MXLEN, and UXLEN before
accepting that mapping. Other supervisor guarantees retain their modeled
extension switches; preserve the exact published UDB input.

The platform test matrix runs the same checked-in, litmus7-generated
smoke selection on both exact tiled simulators. Litmus7 and ELFs are built only
in the shared build jobs; execution retains histogram checking and its own
90-minute job budget. OpenSBI executions retain independent 45-minute budgets
and share firmware only when layout and generated DTB contents match.
CI never runs the full litmus profile. The full profile is manual: build its
ELFs once, then run disjoint name-sorted shards with separate output paths.
`program-test/run.py` checkpoints completed cases atomically after each result,
so an interrupted shard can resume only with identical manifest, simulator,
limits, and shard identity. A partial result retains pending names and must not
count as full-suite success. The runner kills active simulator process groups
on SIGINT or SIGTERM and distinguishes wall from cycle timeouts.
`program-test/report-shards.py` verifies complete, disjoint coverage and a
shared run fingerprint before reporting the full profile; it never runs in CI.

`program-test/run.py` owns ISA, benchmark, CoreMark, Embench-IoT, and Bringup-Bench process-group deadlines,
manifest-declared output contracts, and JSON/JUnit reporting. CoreMark needs
output contracts because its upstream `main` returns zero after reporting
validation errors. The short CoreMark workload uses the standard performance
seeds and requires their exact algorithm CRCs; the expected minimum-duration
warning does not make this functional simulation workload fail. Embench-IoT
instead returns `!verify_benchmark(result)` from its common `main`, so its
ordinary HTIF completion status is authoritative. Its scale and warmup values
are functional-regression controls, not a benchmark score. Keep performance
scoring outside this lane unless the complete upstream timing, normalization,
and disclosure procedure is implemented. ACT retains upstream `run_tests.py`;
`arch-test/report.py` checks its
summary against the full generated inventory. Never interpret an empty or partial
suite as success, and preserve the upstream runner's nonzero status independently
of reporting. Failed generation must stop before DUT execution.

CI builds the patched Sail model once in `arch-sail`, caches it by gitlink,
patch series, compiler version, and installer, then distributes the exact-commit
model artifact to the ACT generation matrix. Each single-core configuration's
generation job restores only an exact-input completed ELF bundle, separately
from the native simulator builds. On a miss it runs full ACT generation; on a
hit it verifies the bundle before republishing it for the current commit. It
publishes a checksum-verified archive with
dereferenced ELF contents, so reference build paths and upstream symlinks
cannot leak into consumers. Four execution jobs per configuration, except eight
for RV5Stage RVA23, need only the native simulator, Python, and upstream
runner, plus the pinned Spike shared libraries for Spike shards—not Sail,
Ruby, Racket, or a compiler.
`arch-test/shard.py` takes every sorted generated ELF and partitions
by index modulo shard count. Its tests enforce disjoint full coverage and safe
replacement of stale shard links. Shard inventories and results are artifacts;
all configured matrix jobs must complete to claim full execution coverage.

Each config CI build publishes `VTestDriver` and its JSON attestation. Consumers
set `PREBUILT_SIMULATOR` to the downloaded executable. This bypasses native
build prerequisites and verifies commit, platform, SoC, and binary hash before
execution; missing artifacts must fail rather than silently build a replacement.
`artifact.py record` is run immediately after a successful simulator build in
the clean CI checkout. The executable uses statically linked FESVR and standard
Ubuntu runtime libraries; Spike additionally needs its pinned shared libraries
at the producer's runtime path. All producer/consumer jobs use the same runner image.

Keep tool downloads checksum-pinned and update compiler/ACT/Sail compatibility
together. UDB 0.1.17 uses `SUPPORTED_PMLEN_SSNPM` instead of an active `PMLEN`;
core projections declare the complete supported set, and the shared Sail adapter
maps those capabilities without selecting a runtime PMM mode. On macOS arm64,
ACT setup preinstalls checksum-pinned native Z3 5.1.0 in UDB's versioned cache.
Cache complete ACT payloads using generated configuration content,
ACT/Sail revisions and patches, compiler/binutils archive and version identity,
Python/Ruby dependency identities, generation options, and adapter inputs, not
the repository commit. `arch-test/payload.py` dereferences each ELF and records
its hash in a nonempty inventory, alongside configuration and archive checksums.
Both cache hits and artifact consumers verify the complete inventory and reject
unsafe archive members before extraction. Cache hits also compare the current
configuration and input key. Invalid bundles fail closed; only successful full
generation and packaging can reach the explicit cache-save step. On a miss,
retain reference intermediates but replace the generated ELF inventory as usual.
Never cache DUT results: every execution shard runs the current simulator.

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
make -C sims tiled-lowering-test SOC=tiled-rv5stage-rva23
```

Run the end-to-end execution path for each supported system with:

```sh
make -C sims smoke SOC=simple-rv5stage-rva23
make -C sims smoke SOC=simple-spike-rva23
make -C sims smoke SOC=mini-rv5stage-rva23
make -C sims smoke SOC=tiled-rv5stage-rva23
make -C sims host-mmio-test SOC=simple-rv5stage-rva23
make -C sims host-mmio-test SOC=mini-rv5stage-rva23
make -C sims host-mmio-test SOC=tiled-rv5stage-rva23
make -C sims boot-test SOC=simple-rv5stage-rva23
make -C sims boot-test SOC=mini-rv5stage-rva23
make -C sims boot-test SOC=tiled-rv5stage-rva23
make -C sims uart-pty-test SOC=simple-rv5stage-rva23
make -C sims uart-pty-test SOC=mini-rv5stage-rva23
make -C sims uart-pty-test SOC=tiled-rv5stage-rva23
```

FESVR's write-data wrapper retains lane placement, masks, and packet-position
policy while using [`chi/protocol/messages.rhdl`](../chi/protocol/messages.rhdl) for immutable
`NonCopyBackWriteData` construction. DataID is derived from the address for
both cacheable and noncacheable transfers; preserve that packet position plus
the explicit CCID and DBID/MECID choices independently of the core requester
profile.

FESVR REQ construction is also immutable, with the same inactive-field zeros.
Keep opcode selection, cacheable/device attributes, address/size, and NodeID
conversion in this requester rather than sharing core policy. The `fesvr-mmio`
bench compares complete emitted requests and holds them through backpressure.

The transport checks require the pinned FESVR library; DPI checks also require
Verilator. Lowering requires the pinned CIRCT tool or an explicit `CIRCT_OPT`,
and execution requires FESVR plus the RISC-V cross compiler. Rhombus checks use
repository wrappers with persistent worktree-specific compiled roots.
Technology-mapped simulation remains owned by
[`../vlsi/sim/`](../vlsi/sim/README.md).

`uart-pty-test` starts the ordinary simulator, discovers the production PTY path,
and exchanges all 256 byte values with a polling UART payload. Each byte is
checked by both the target and the external Python client, with transformed
replies, a final acknowledgement before process exit, and bounded cycle/wall
timeouts. It exercises the actual core, CHI MMIO, UART FIFOs, serial engines,
and PTY; no test-only DPI transport bypasses that path. Simulation CI runs it
on all three SoCs. Keep the UART C++ source/header in both ordinary and mapped
simulator link prerequisites when changing this shared harness dependency.

`transport-test` exercises the pinned FESVR `memif_t` path, exact-width and zero
writes, backpressure, idle yields, and target errors. `DirectMemoryHtif::tick`
advances request/response handshakes every cycle, but resumes the host context
only when no request remains pending. A pending host transaction is suspended
in `transact()` until completion; startup and idle yields still resume on each
tick. Completion resumes the host on the same tick, including error propagation
and successful-write observers. The backend `fesvr-mmio` fixture tests
the DPI-independent `FesvrCHIAccess` engine with coherent RAM fragmentation,
exact MMIO, response validation, and backpressure. `host-mmio-test` loads ELF
data into the UART scratch register, verifies the published ELF entry and
preserved UART value, and reads a device signature back through FESVR.

`DirectMemoryHtif::reset()` is FESVR's post-loading startup callback, not the
hardware reset signal. It validates the entry, writes each selected ACLINT MSIP,
then publishes the entry with one blocking eight-byte transaction before ordinary
HTIF polling resumes. Nonzero harts are woken before hart zero; every selected
hart remains in the BootROM polling loop until the final publication. Every loading
transaction is also blocking, so no separate drain or boot arbitration state
machine is needed. Loading writes and clears overlapping the configured register
are rejected in C++; ordinary post-publication accesses remain allowed.
The register address and XLEN come from the harness through DPI. RHDL and the
C++ transport each define the fixed architectural ACLINT base directly. The
exact selected hart IDs come from the consumed `+boot-harts=` option; the host
never receives a hart count. `FesvrRequester` connects the
transport directly to the generic `FesvrCHIAccess`; target/protocol failures
return through its normal response and become transport failure exits.

The native `transport-test` covers selection parsing and release order,
relocated registers, request/response stalls, loading and publication failures,
RV32/RV64 entries, zero entries, and overlapping
writes and clears. `boot-test` exercises actual FESVR and the indirect ROM at two
different ELF entry points on every SoC. These tests run in simulation CI.

The Zicboz payload checks all 64 offsets and neighboring blocks through the
normal FESVR flow. Its final signature also lets FESVR read the dirty cache
line coherently after the program exits:

```sh
make -C sims zicboz-test SOC=simple-rv5stage-rva23
```

`zihintntl-test` runs `tests/programs/zihintntl.S` through the RV5Stage
specialization of the shared single-core harness only. The payload's conflict bank is intentionally
matched to SingleCoreRV5StageSoC's 64-set, four-way L1D profile; changing that profile
requires revisiting this test rather than silently reusing it for another SoC.
It calibrates warm and cold accesses on the running system, then compares minima
over repeated trials to tolerate unrelated HTIF snoops. The second hinted read
must remain a miss, and an intervening dirty line must retain its authoritative
value. The inclusive outer cache may invalidate that L1 copy while allocating
the hinted line, so this SoC test does not require the resident probe to hit;
the L1 no-replacement property is covered by
`cores/rv5stage/tests/circt/verilog/rv5stage-dcache_tb.sv`. Do not weaken the NTL check to
data-only checks: ignoring NTL preserves architectural values and would
otherwise pass. The payload selects S-mode Sv39 data
translation through MPRV while executing in M-mode; test addresses are virtual
aliases outside the physical RAM window, so bypassing translation cannot pass.
It needs no supervisor runtime. Compressed and FP subcases are selected from
`misa`. MiniRV5StageSoC and TiledSoC may enable Zihintntl but are not covered by this
geometry-specific end-to-end target.
