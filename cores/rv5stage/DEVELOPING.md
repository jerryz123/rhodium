<!-- Guides contributors through RV5Stage implementation ownership, diagrams, and validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RV5Stage

Read the core [README](README.md) for the pipeline, completion, ordering,
system, memory, privileged-state, and deliberate-limit contracts. This guide
owns implementation placement, change sequencing, generated diagrams, and
focused validation.

## Architecture and dependency boundary

RV5Stage may depend on public Rhodium libraries, the pure RISC-V model and RTL
adapter, reusable components directly under `cores/`, HardFloat, and shared
CHI libraries. It must not import another named core, a backend, examples, or
tests. The parent [`check-boundaries.sh`](../check-boundaries.sh) enforces these
rules plus decode-column and cache-package separation.

Keep the scalar pipeline dependent on the RV5Stage cache protocols rather than
a generic memory transport. Keep I-cache and D-cache packages independent of
each other; share external transaction machinery through the CHI package.

## Implementation map

| Area | Ownership |
|---|---|
| [`profile.rhm`](profile.rhm) | Immutable ISA, MMU, and cache specialization description |
| [`udb.rhm`](udb.rhm) | Exact-version UDB extension closure and fixed RV5Stage architectural parameter claims |
| [`rv5stage.rhdl`](rv5stage.rhdl) | Core, MMU, prefetch routing, cache, uncached, and CHI composition |
| [`core.rhdl`](core.rhdl) | Scalar pipeline, forwarding, hazards, commit, and deferred completion |
| [`bundles.rhdl`](bundles.rhdl) | Scalar pipeline payloads |
| [`btb.rhdl`](btb.rhdl) | Associative word lookup, local direction counters, training, and prediction metadata |
| [`../cache-prefetch.rhdl`](../cache-prefetch.rhdl) | Reusable best-effort prefetch operation and request types |
| [`frontend.rhdl`](frontend.rhdl), [`frontend-control.rhdl`](frontend-control.rhdl) | Fetch topology, fixed-latency S1/S2 correlation, registered repair, and independent execution controls |
| [`fetch-source.rhdl`](fetch-source.rhdl) | S0 PC selection, registered S1 prediction, continuation state, and replay selection |
| [`fetch-packet.rhdl`](fetch-packet.rhdl), [`fetch-scan.rhdl`](fetch-scan.rhdl) | Raw packet boundary and S2 prediction-cut validation |
| [`instruction-buffer.rhdl`](instruction-buffer.rhdl) | Core-owned fall-through compressed assembly and one residual halfword |
| [`decode/DEVELOPING.md`](decode/DEVELOPING.md) | Structured integer and FP control generation |
| [`register-file.rhdl`](register-file.rhdl) | Two-read, two-write integer register bank |
| [`vector/DEVELOPING.md`](vector/DEVELOPING.md) | Standalone flat vector bank, operand/result packing, and composed SIMD validation |
| [`fp/DEVELOPING.md`](fp/DEVELOPING.md) | FP payloads, register state, execution lanes, LSU bridges, and completion |
| [`csr.rhdl`](csr.rhdl), [`interrupt.rhdl`](interrupt.rhdl) | Privileged state, traps, counters, and interrupts |
| [`mmu/DEVELOPING.md`](mmu/DEVELOPING.md) | TLBs, demand translation, best-effort prefetch probes, and page-table walking |
| [`instruction-memory-router.rhdl`](instruction-memory-router.rhdl), [`memory-router.rhdl`](memory-router.rhdl), [`uncached-protocol.rhdl`](uncached-protocol.rhdl) | Physical-region routing, data IO-MSHR composition, and the shared uncached protocol |
| [`cache.rhdl`](cache.rhdl) | Shared cache geometry and replacement helpers |
| [`chi/DEVELOPING.md`](chi/DEVELOPING.md) | Physical-region/Home policy, RN identity, cache transaction engines, and the shared uncached RN-I implementation |
| [`icache/DEVELOPING.md`](icache/DEVELOPING.md), [`dcache/DEVELOPING.md`](dcache/DEVELOPING.md) | Private cache implementation and validation |
| [`tests/`](tests/) | Decode, configuration, public specialization, and invalid-use checks |

## Change the core

1. Identify the owning boundary before editing: decode, scalar pipeline,
   deferred completion, architectural state, translation, cache, CHI engine,
   or top-level composition.
2. Preserve single-issue ordered scalar commit while tracking every deferred
   register-producing operation through its scoreboard and completion path.
   Ordinary loads and stores launch their virtual SRAM read in EX. MEM performs DTLB,
   physical-tag, permission, and lane selection; a permitted cache hit becomes
   the normal MEM/WB result, or retains an owned store candidate for WB enqueue.
   Replay resource conflicts through the same pipeline, independently of slow
   miss service and faults. WB remains the authorization boundary for miss
   transactions, device reads, mutations, FP compute, hints, and reservations.
   A speculative lookup must never allocate, mutate, reserve a destination, or
   start device IO. Rejection replays before transaction acceptance; accepted
   transactions must never be replayed. Keep coherence service independent.
3. Add architectural state and serialization rules before integrating an
   execution unit that depends on them. Keep F/D/Zfh specialization host-side
   so disabled hardware elaborates away.
4. Preserve exact fault ownership and priority across Fetch, MMU, PMA routing,
   caches, Execute, Memory, and WB. Do not collapse speculative flush with
   architectural invalidation.
   Keep integer bypass selection in Decode and register it with the captured
   operands. `RV5StageBypassStage` is an exactly-one-hot enum selecting captured
   register-file data, MEM, or normal WB. Resolve newest-producer priority and
   the register-file fallback in Decode; EX uses the enum's typed `.mux`.
   Selectors remain legal during bubbles and are asserted outside token validity.
   Do not qualify forwarding with live MEM fault/replay/kill results;
   those cancel younger token validity, independently of payload capture.
   Use `ValidPipeAlwaysCapture` for these stage boundaries.
   EX's WB bypass reads `wb_input.bits.value`, not the retirement-context
   mux `wb_offer_bits.value`. The `rv5stage-core` emitter guards ALU operand roots
   against live control or retained contexts, including a positive check for
   the normal WB value source on both operands.
   A MEM load hit may select the normal WB bypass for the following EX cycle;
   an EX load still interlocks its immediate dependent. Hit data and the result
   classification must cross MEM/WB together. FP hits use `load_hit` at WB
   without allocating a deferred reservation; older data transactions must be
   drained before admitting that path so the FPR load-write port cannot collide.
5. Test cycle-visible behavior in the narrowest CIRCT/Verilator fixture, then
   the composed core. Do not add an elaboration snapshot for every submodule.
   Update [README.md](README.md) when public profiles, ports, ordering, timing,
   or deliberate limits change.

Keep frontend stage ownership explicit: execution owns architectural redirects,
invalidation, training, and instruction consumption. The separate frontend owns
PC selection, S0/S1/S2 attempt contexts, replay, prediction scanning, and raw packets.
The core owns instruction assembly and compressed expansion directly in Decode.
MMU owns S1 translation/PMA and accepted walks; L1I owns SRAM lookup and accepted
refills. Neither retains an ordinary fetch request for later response.

S0 reserves space from registered packet-queue occupancy plus S1/S2 validity.
Never borrow same-cycle dequeue credit or feed Decode readiness into S0.
The fetch source forks accepted `RV5StageFetchAttempt` offers to memory and S1.
BTB lookup uses the registered S1 PC, and its result travels with that occurrence
into S2. Prediction chooses the next S0 PC; a stall retains that decision.
The only outcome-to-PC feedback is registered S2 replay selecting the oldest
failed attempt's PC and continuation context. It kills younger S1 work without
clearing older packets or core residual state. Architectural recovery clears the
packet queue, IBuf residual, and both stages. Registered prediction repair clears
only younger attempts: the scanner already corrected the current packet.
The host `fetch-admission-test.rhm` guards these timing boundaries using
hierarchical port-leaf dependencies.

The frontend uses `ShiftQueue(Packet, 5, ~flow: #true)` without same-cycle full
replacement. A reserved S2 result converts through checked `to_decoupled` into
the queue. Empty-queue bypass feeds the core IBuf and Decode in the S2 cycle;
there is no separate IF/ID register. The IBuf stores only one leftover halfword
with its PC and prediction metadata; faulting packets remain in the queue until
consumed. Packet consumption is not instruction
consumption: an incomplete instruction may consume a packet without issuing,
and a retained compressed instruction may issue without consuming a packet.

The S2 scanner keeps its own partial-instruction bit for fetch lookahead,
independently of the core's residual parcel. It validates branch locations and
lengths without full compressed expansion. A stale cut is removed from the
current packet; a registered repair invalidates the BTB entry and restarts after
that packet, preserving older queued instructions. Architectural recovery wins.
A source clear without restart stops admission until an explicit restart; never
reconstruct recovery from the core's assembly cursor or speculative next PC.

Frontend S1/S2 and the repair pipeline use flushable Valid pipes with explicit
`flush` inputs. Recovery clears their validity at the edge while preserving
the global reset domain and always-capture payload timing. Retain the existing
same-cycle output filters: synchronous flush does not gate pre-edge transfers.
The intrinsic pipe contract exposes flush to event lineage instrumentation.

Keep flow conversions at their actual timing boundaries. MMU and L1I stage
results derive from their existing Valid context through filters and maps;
S2 always produces an outcome, including replay when its implementation has
no reply. Never introduce a queue or asynchronous join for fixed-cycle pairing.
Demand and best-effort prefetch offers share a demand-priority L1I arbiter,
then accepted events fan out to SRAM commands and lookup context. Refill
completion remains held through the final installation word. State-owned
producers may drive their interface fields directly; extracting an existing
stream's fields solely to inject or eject them elsewhere is unnecessary.

Core restart, invalidation, and training derive from live MEM/WB flows.
Keep independent events separate; priority arbitration applies to competing
restart targets, while flush events coalesce. The reset-owned start pulse is
a genuine source. Nonbranch MEM instructions must still train the BTB so a
stale prediction at their PC can be removed.

`rv5stage-fetch-throughput` uses the real frontend/MMU/router/L1I path. It
requires consecutive aligned, straddling, and compressed instructions in each
cold-refilled line's interior, without a warming restart, and separately checks
warm throughput. Architectural core programs use `tests/core-fixture.rhdl`:
a test-only ordered word-memory model drives explicit replay, not cache timing.
Do not use that model to claim frontend throughput or LR/SC progress.

Keep predictor updates in MEM behind older-WB cancellation, faults, and replay.
Update by current PC match rather than a stale entry index; invalidation wins
over training. Preserve `sequential_pc` for links and `predicted_next_pc` for
recovery as distinct payload fields. The BTB is ordinary named-core RTL, not a
new language feature or ISA profile parameter.

For predictor changes run `rv5stage-btb`, `rv5stage-fetch-prediction`, and
`rv5stage-branch-prediction`, then existing `rv5stage-fetch`, `rv5stage-core`,
and fault/replay fixtures. The paired core test compares actual stores, cycle
counts, flushes, and consecutive backedge requests with prediction enabled and
disabled; it does not inspect internal predictor state. The fetch test covers
compressed branch ordering, continuation words, duplicate-PC occurrences,
backpressure, stale-cut repair, and precise continuation faults.

Mul/div dispatch validity comes from authorized commit, but operand payloads
come directly from the normal WB pipeline token. Retained CMO and WRS retirement
contexts must not select arithmetic operands. The reusable multiplier captures
raw operands before its magnitude-preparation cycle; keep that register boundary
between WB selection and full-width negation.

## Pointer-masking ownership

The opt-in RV64 Ssnpm path uses reusable policy and address helpers from
`riscv/rtl/pointer-masking.rhdl`. `csr.rhdl` owns PMM state and WARL writes,
and reexports the shared `PrivilegeMode` for existing core consumers.
ID captures `PointerMaskControl` in `DecodeExecute`; EX transforms only the
effective memory address and leaves the integer result and low 48 bits intact.
The transformed address feeds speculative lookup, registered WB requests,
explicit prefetches, replay correlation, and address-fault values. Fetch,
branch targets, implicit PTE requests, and CSR writes never pass through it.

CSR serialization keeps policy stable across younger ID admissions. A committed
PMM change uses the ordinary serializing restart, killing younger work and
clearing the MMU's prefetch stages through the fetch-flush path. It does not
assert `translation_flush`: PMM changes neither PTEs nor translation tags.
Accepted older memory operations drain before the CSR commits. Ordinary trap
and return redirects continue to establish the next instruction's context.

Keep the implementation opt-in and unadvertised until platform qualification.
Run `pointer-masking-test.rhm`, `profile-test.rhm`, `riscv-pointer-masking`,
`rv5stage-pointer-masking`, and `rv5stage-csr`; include `rv5stage-mmu-replay`
when modifying the shared effective-data-privilege helper. The core fixture
checks policy changes, tagged payload preservation, replay, all prefetch kinds,
and transformed misalignment trap values without relying on internal nets.

## Pipeline event annotations

The Flow stage modules own their public interface trace contracts; `core.rhdl`
does not redeclare them on instances. Keep storage certification local to its
implementation; do not replace
always-capture payload registers or derive controls from generated signal names.
EX's payload is still computed unconditionally; its flow filter qualifies only
token validity, preserving the feed-forward datapath and cancellation timing.

Accepted `frontend.s0.request` occurrences are explicit roots. The intrinsic
flushable pipes connect them through `frontend.s1.lookup` and
`frontend.s2.outcome`, which captures replay, admission, and admitted fault flags.
Only admitted outcomes pass the Flow filter into packet storage; no
additional checkpoint represents that same-cycle admission. ShiftQueue owns
its shifting-window contract and explicit flush. Core-side assembly declares
a depth-one window using actual residual capture/release, clear, and independent
resident/live contribution predicates. A word can parent two compressed
instructions; a straddle has two word parents, including a faulting continuation.
`core.s2.decode` inherits those
parents instead of cutting ancestry. Retry attempts are new roots, and MMU,
I-cache refill, predictor-training, and redirect causality remain separate.
Run `event-window`, `event-frontend`, `rv5stage-fetch-prediction`, and
the host admission test for changes at this boundary.
Later checkpoints must not become independent roots to hide an unsupported
path. Decode transfers fire only when the hazard gate admits them,
but its checkpoint must precede `gate_flow` and follow the squash filter so
stall observations see valid instructions while issue is blocked. Keep the
captured Boolean reason terms aligned with `pipeline_hazard`; do not impose
priority on simultaneous reasons. Keep WB arrival distinct
from architectural retirement and deferred completion.

Fork the live MEM observation for `dcache.s1.access` and filter WB memory
operations for `dcache.s2.resp`. Select by access kind, not the slow-request
valid bit, so fast hits and replays remain visible. These observations retain
their core-stage parents; do not add registers or override intrinsic storage
contracts to manufacture a direct S1-to-S2 edge. WB uses `offer_decoupled()`
after S2 for the Valid-to-Decoupled slow request. Record nonfaulting admission
in S2 without feeding readiness/fault status into functional request validity.
The cache's named queue, S3 elastic pipe, and S4 always-capture register carry
their own trace models. Observe S3 before its advance gate (including stalls)
and S4 before its hit/transaction demux. Capture direct refill acceptance and
command fields on S4, before arbitration with post-eviction commands; do not
certify the gather FSM as a pipe.
Walker and admitted prefetch sources are explicit independent roots.
`rv5stage-load-hit` instruments the actual core/MMU/router/cache composition;
its DPI scoreboard checks S1/MEM and S2/WB alignment, one-cycle S1/S2
correspondence, public admission against S2 fields, FIFO ancestry through
S3/S4, exact miss PCs and direct refill addresses, and S4 ownership across
backpressured CHI attempts, RetryAck, and PCrdGrant. Refill owns the scoped
command-to-attempt contract before its Flow request mapper; unrelated engine branches explicitly detach before
request arbitration.

After edits, run `rv5stage-core` for forwarding, stalls, replay, redirects, and
deferred completion, then the SimpleSoC trace smoke. Its native Perfetto checks
follow exact occurrence edges, compare named PC/instruction captures, and
check one-cycle or elastic delays without requiring every fetched
token to survive. The conversion/fanout compiler fixture covers both Valid
replication outputs, including a dropping branch.

Keep private-cache outer-channel observations in `rv5stage.rhdl` at the
L1I/L1D-to-CHI composition boundary. The local connection helper preserves all
available ready-valid channel directions and captures only named scalar metadata.
Select each channel's enum opcode with `~format: "enum", ~label: #true`; do not
hand-maintain REQ/RSP/DAT/SNP decoding tables in the exporter. Check decoded slice
names independently of numeric opcode captures in the trace smoke.
Do not infer CHI transaction ownership by matching TxnID/DBID values. Request
checkpoints are nonterminal so Flow can carry them through network transit.
D-cache incoming RSP/DAT observations inherit certified Home output ancestry;
the inclusive Home's retained request scope bridges its FSM. Outgoing RSP/DAT,
snoops, and instruction-return channels remain independent observations.
Refill acknowledgement/completion and backing-memory provenance remain separate.
Enable stall companions without requiring activity on idle channels.
After changing them, run the SimpleSoC trace smoke; its
`check-cache-events.sql` checks schemas, real miss/refill traffic, endpoint IDs,
and the lack of fabricated parent edges. Instruction RN-I has no SNP channel;
only the data cache contributes snoop events. Use a cache-heavy benchmark to inspect
additional writeback/snoop activity; an idle channel need not emit an event.
`check-stall-events.sql` separately validates admitted packet ancestry for Decode
stalls and reason flags without counting observers as transfer fanout.

## Maintain the UDB projection

Keep selectable extension membership derived from `RVCoreProfile`. Keep fixed
CSR, trap, alignment, counter, PMP, and LR/SC facts in `udb.rhm`, and pass
physical address width and PMA granularity from the integration boundary. When
one of those behaviors changes, update its RTL owner and UDB claim together.

Run the pure UDB encoder and RV5Stage projection tests, generate a concrete
configuration, and validate it with the UDB version pinned by the ACT4 checkout:

```sh
tools/run-racket-tests.sh riscv/tests/udb-test.rhm cores/rv5stage/tests/udb-test.rhm
make riscv-udb-config RISCV_UDB_CONFIGURATION=simple-soc
bundle exec --gemfile riscv/riscv-arch-test/framework/src/act/data/Gemfile \
  udb validate cfg /tmp/rhodium-udb/simple-soc.yaml
```

UDB semantic validation is required before changing an extension version or
parameter set because schema validation alone does not detect missing
extension-dependent parameters. Keep generated YAML out of version control.

## Generated detailed diagrams

The README diagrams describe architectural intent. Generate an implementation
inventory of the elaborated RV64 core, including child blocks, registers, and
typed interface channels, with:

```sh
mkdir -p /tmp/rv5stage-core-diagram
env PLTCOMPILEDROOTS="$(mktemp -d)" \
  racket -y -S "$PWD" tools/write-rv5stage-core-diagram.rhm \
  /tmp/rv5stage-core-diagram
```

The source is
[`../../examples/rv5stage/core-diagram.rhdl`](../../examples/rv5stage/core-diagram.rhdl).
The JSON targets interactive renderers; the compact DOT view links child
modules by name instead of flattening them.

## Focused validation

For the EX/MEM/WB load path, run `rv5stage-load-hit`, `rv5stage-mmu-replay`,
`rv5stage-dcache`, `rv5stage-dcache-rv32`, and `rv5stage-core`. The integrated
load-hit fixture checks a cold fill, one-bubble dependent addresses, bubble-free
vvadd-style warm loads, signed/unsigned lanes, FP hits, exactly-once device
reads, store-to-load ordering, and a younger lookup squashed by an older WB
fault. Keep the timing regression at the real core/MMU/router/L1D
boundary rather than replacing the cache with a fixed-latency response stub.
Like the complete-core IO-boot fixture, it uses the SoC harness's Verilator
UNOPTFLAT setting for packed-interface scheduling; assertions and runtime
convergence checks remain enabled. IO-MSHR request readiness is registered state,
not a dependency on its request payload or whole `drained` output bundle.
Include `rv5stage-fp-pipeline`, `rv5stage-core-rv32f`, and `rv5stage-core-rv64d`
for FP-hit writeback or shared payload changes.

### Ziccrse progress gate

The full-core qualification matrix for the [Ziccrse integration
guarantee](README.md#lrsc-eventuality-ziccrse) is:

```sh
FIXTURES='rv5stage-lrsc-core-progress rv5stage-lrsc-core-progress-predicted rv5stage-lrsc-core-progress-rv32' \
  bash tests/backend/run-circt.sh --simulate-only
```

It executes sixteen-instruction constrained LR.W/SC.W and RV64 LR.D/SC.D loops through RV5Stage,
its MMU and 32-set direct-mapped L1s, a two-set/two-way inclusive Home, and
CHI SRAM. The three instruction placements are `0x1fc0`, `0x1fe0`, and
`0x1ffe`; Bare and supervisor Sv39 runs cover both ordinary and halfword-offset
fetch/page crossings. Sv39 uses separate 4-KiB leaves; RV32 uses Bare mode.
The original fixture disables prediction, while `-predicted` and `-rv32` use
the production 16-entry predictor. Each placement runs without extra traffic,
with read-only LLC conflicts, with competing LRs, and with competing LR/SCs.
Pressure cases take six forward branches over NOPs. Competing LRs must permit
core progress; competing SCs check system progress rather than per-hart
starvation freedom. There are 48 cases per RV64 fixture and 12 RV32 cases.
All program loading and signatures
use coherent requests. Initial fetch is stalled during loading, but the
caches remain alive to service broadcast snoops.

The original twelve cases passed with nonsnooping instruction residency and accepted
instruction walks that survive ordinary fetch recovery. Previously both Sv39
`0x1fc0` cases repeatedly canceled a younger ITLB walk on SC replay before it
could fill the TLB. The MMU now detaches the squashed consumer while preserving
the walk and its PTE-response ownership; successful translation survives for
refetch. No reservation-timer extension, PTE allocation-policy change, or
speculative-walk throttle was needed for this regression. The bench reports
every failed case and then fails overall; recovery is diagnostic evidence,
never a passing result.
Before the progress cases, the same bench executes a self-modifying-code
program: warm an instruction line, modify it through the core's dirty data
cache, execute `FENCE.I`, and call the updated code.
The [SoC qualification](../../sims/DEVELOPING.md#lrsc-system-qualification)
adds real MiniSoC, SimpleSoC, and eight-hart TiledSoC memory paths through normal FESVR.
The expanded qualification passed on 2026-09-08 against `d78ce435` production
RTL: 108/108 core cases, all three FENCE.I checks, 12/12 SimpleSoC placements,
12/12 eight-hart TiledSoC placements, and 81/81 concrete memory-map checks.
MiniSoC subsequently passed 12/12 placements on the same date through its
ordinary harness and forwarding HN-F/internal CHI RAM. Its compressed-disabled
profile uses word-aligned page-boundary starts and page tables inside its
64-KiB RAM; the six placements still cover LR.W/SC.W and LR.D/SC.D in both
Bare and Sv39. The adapted shared payload also passed all 24 SimpleSoC/TiledSoC
placements on their previously qualified simulators.
Only qualification fixtures, payloads, build targets, and documentation
changed; no further production RTL fix was required for this matrix.
MiniSoC, SimpleSoC, and TiledSoC now enable the explicit `ziccrse` profile claim;
generic profiles remain opt-out. Preserve the matrix as a regression
gate when changing fetch, translation, reservations, or coherence arbitration.
Do not enable another integration from cache-only results or treat this bounded
regression as a proof for every translation and fabric-fairness scenario. In particular,
extending the reservation timer alone does not establish progress across
translation arbitration and replay.

For Ziccif/Ziccamoa validation, select `rv5stage-fetch`, `rv5stage-icache`,
`rv5stage-dcache`, `rv5stage-dcache-rv32`, `rv5stage-memory-router`,
`chi-coherent-home`, and `chi-inclusive-home`. The L1I regression cancels lookup and refill installation on architectural
invalidation and checks fresh words at all sixteen offsets, with reversed/gapped
data packets. The instruction-coherence fixtures additionally check dirty-owner
intervention and residency independent of outer-cache replacement. Fetch assembly separately covers aligned words and compressed parcels.
The L1D regressions check all nine AMOs at RV32 word and RV64 word/doubleword
widths, old-value returns, signedness, overflow, byte-lane preservation,
ownership-delayed miss completion, and a snoop contending with an accepted RMW.
The memory-router fixture admits all nine AMOs at coherent RAM boundaries and
rejects them for device and non-atomic regions.
Home fixtures exercise coherent ownership and authoritative dirty-data handling.
These are component-level behavioral regressions, not an exhaustive concurrent
multi-hart memory-model proof. Keep AQ/RL ordering and LR/SC progress distinct
from the memory-region AMOArithmetic capability.

Run `tools/run-racket-tests.sh socs/tests/main-memory-test.rhm` to audit concrete
SoC PMAs. The test checks every cacheable HN-F region, including sparse tiled
bank masks, for executable, readable, writable, idempotent, non-device,
atomic-capable RAM and exact coverage of described memory. Generic
`RV5StageCHIConfig` still allows restricted cacheable maps. The top-level
`RV5Stage` elaboration checks them against the selected `ziccif`/`ziccamoa`/`ziccrse`
profile claims before instantiating hardware. Keep the default SoC profiles,
profile/UDB projection tests, and per-hart DTB checks aligned when changing
these claims. Run `profile-test.rhm`, `udb-test.rhm`, and `rv5stage-test.rhm`
under this directory's `tests/`, plus `socs/tests/udb-test.rhm` and
`socs/tests/run-device-tree.sh` from the repository root.

For Zic64b/Za64rs, run `cores/rv5stage/tests/profile-test.rhm`,
`cores/rv5stage/tests/udb-test.rhm`, and `socs/tests/udb-test.rhm` in one host
batch, then `bash socs/tests/run-device-tree.sh`. Validate generated RV32 and
RV64 UDB configurations as described above; `Za64rs` requires the implied
`Za128rs` entry, and `Zic64b` requires `CACHE_BLOCK_SIZE` even with CMO disabled.
Account for the [UDB 0.1.16 applicability limitation](README.md#cache-block-and-reservation-bounds)
when validating CMO-free configurations. Do not omit the hardware fact or
silently enable CMO decode to satisfy that database version.
Select `rv5stage-icache`, `rv5stage-dcache`, and `rv5stage-dcache-rv32` for
CIRCT/Verilator validation. The data-cache benches cover all 64 CBO byte offsets,
aligned word/doubleword LR/SC sites on both sides of a 64-byte boundary,
neighboring-line isolation, exact SC matching, and one-shot reservation use;
the RV64 bench also covers invalidating snoops. These size/boundary regressions
do not establish eventual LR/SC success under adversarial coherence traffic.

For Zkt, run the architecture/profile/advertisement checks and the RV32 and
RV64 integer timing fixtures:

```sh
export PLTCOMPILEDROOTS="$(mktemp -d)"
tools/run-racket-tests.sh riscv/tests/zkt-test.rhm tests/backend/rv5stage-zkt-test.rhm cores/rv5stage/tests/profile-test.rhm cores/rv5stage/tests/udb-test.rhm socs/tests/udb-test.rhm
FIXTURES='rv5stage-zkt-rv32 rv5stage-zkt-rv64' bash tests/backend/run-circt.sh --simulate-only
bash socs/tests/run-device-tree.sh
```

The program generator intersects implemented catalogs with the pure architectural
Zkt family list and encodes instructions through their descriptors. Keep coverage
complete when adding an instruction to that intersection. No cross-compiler or
checked-in generated program image is needed. Two full cores receive identical
instruction streams and scheduling but different operands; compare every public
fetch/data control event, including dependent consumers and deferred hazards.
The separate RV32F/RV64D core fixtures cover FP-enabled specialization and WB
integration; repeating the integer-only Zkt program in those configurations
does not add an FP timing claim.

A separate public-component rig forces load/multiply completion overlap and sink
backpressure, checking exact fixed multiplier latency and retained arithmetic
results. Keep it aligned with the core's four-input completion priority. These
are differential RTL regressions plus a source-level timing design argument,
not exhaustive formal noninterference or physical side-channel certification.
Review operand-to-control dependencies whenever changing forwarding, hazard
gating, iteration termination, or completion selection. In particular, a
zero-operand early exit in the multiplier must fail this regression.

For NTL WB association and request propagation, select `rv5stage-ntl`,
`rv5stage-mmu-replay`, `rv5stage-memory-router`, and `rv5stage-dcache`.
The core bench checks all four selectors, non-memory consumption, replacement,
integer/FP memory, rejected request replay, branch squash, synchronous traps,
interrupt entry, and absence of an older-load drain. Adapter benches check
locality preservation and walker isolation. The `rv5stage-dcache` and
`rv5stage-dcache-rv32` benches check coherent non-allocating load misses,
resident-line preservation, destination/lane handling, and unchanged default
allocation; the RV64 bench also checks retry, CompAck backpressure, and LR state.
`memory.rhdl` owns the locality vocabulary, independently of decode selectors
and cache policy. Keep the entire request in lookup/transaction context rather
than reconstructing metadata at refill completion.

For PAUSE, run the catalog/overlay/profile tests and `rv5stage-pause`, plus
`rv5stage-wfi` and `rv5stage-zawrs` when changing issue/interrupt gating.
`decode/hint-ctrl.rhdl` owns the nonarchitectural hint selector. Pipeline
payloads carry it to WB, where PAUSE retires before starting a bounded counter.
The hint's younger-issue barrier is distinct from system/fence serialization:
PAUSE must not inherit an older-work drain. Keep interrupt and completion
service outside the cooldown gate.

For WB-owned Zawrs waiting and the cache-owned reservation observation path:

```sh
FIXTURES='rv5stage-zawrs rv5stage-wfi rv5stage-dcache rv5stage-memory-router rv5stage-mmu-replay' \
  bash tests/backend/run-circt.sh --simulate-only
```

The WRS bench checks retirement deltas through CSRs, original trap PC/value,
globally masked and enabled interrupt wake, timeout and privilege policy, and
invalidation before and during entry. Cache and adapter benches cover the
reservation level independently of instruction waiting. Keep pending WRS
context in the core, LR/SC state in L1D, and timer/interrupt policy independent
of physical clock gating. Select Zawrs through `RV5StageExtensions`, keeping
core decoder selection, ISA descriptions, and UDB claims derived from that
same profile. The UDB database names the ratified extension version `1.0.0`.

For Zihpm CSR catalogs, profile claims, and access semantics, run:

```sh
export PLTCOMPILEDROOTS="$(mktemp -d)"
tools/run-racket-tests.sh riscv/tests/csr-test.rhm tests/frontend/riscv-csr-bank-test.rhm cores/rv5stage/tests/profile-test.rhm cores/rv5stage/tests/udb-test.rhm
FIXTURES='rv5stage-zihpm-rv32 rv5stage-zihpm-rv64 rv5stage-csr' \
  bash tests/backend/run-circt.sh --simulate-only
```

The two Zihpm benches share an XLEN-parameterized sweep of every HPM slot,
write-ignore behavior, read-only write intent, RV32 high halves, and S/U
access denial. Keep the CSR-bank grouping in the RISC-V adapter and profile
permission policy in this core; do not add counter state for zero-valued slots.

For data IO-MSHR admission, ordering, and shared RN-I contention, run:

```sh
FIXTURES='rv5stage-memory-router rv5stage-uncached rv5stage-io-mshr rv5stage-io-boot' \
  bash tests/backend/run-circt.sh --simulate-only
```

The router fixture covers RV32 permission rejection and cached/uncached
exclusion. The composed IO-MSHR fixture covers RV64 retained payloads, fetch
arbitration and cancellation, backpressure, exactly-once completion, and reset.
The complete-core boot fixture executes the generated polling ROM with delayed
entry publication, secondary-hart parking, and fence-ordered signature stores
at three CHI response latencies. It requires one L1I line read for the polling
ROM and one for the payload, allowing additional distinct speculative lines,
with no D-cache traffic or ROM CompAck. This full-core fixture uses the SoC harness's
Verilator `UNOPTFLAT` warning setting for packed interfaces; assertions and
runtime convergence checks remain enabled. Keep simulator entry programming and SoC
BootROM policy separate from this core-level regression.

For instruction-router flow changes, select `rv5stage-instruction-memory-router`
and `rv5stage-io-boot`. The router bench covers registered cached outcomes, S1 kill, explicit replay,
exactly-once uncached acceptance, stalled IO, and flush cancellation.

For Zicbom, use the composed decode test and the WB, MMU, physical-router,
and self-snooped cache fixtures:

```sh
export PLTCOMPILEDROOTS="$(mktemp -d)"
tools/run-racket-tests.sh cores/rv5stage/tests/zicbom-test.rhm cores/rv5stage/tests/rv5stage-test.rhm cores/rv5stage/tests/udb-test.rhm
FIXTURES='rv5stage-zicbom rv5stage-csr rv5stage-mmu-replay rv5stage-memory-router rv5stage-dcache rv5stage-dcache-rv32' \
  bash tests/backend/run-circt.sh --simulate-only
```

Keep retirement context in the core, reusable xenvcfg policy in `riscv/rtl`,
and transaction lifetime in `CHICacheMaintenance`. The data response's
`access_fault` is a completion status, distinct from pre-acceptance request
faults. Ordinary accesses currently produce successful completion status;
do not silently generalize asynchronous ordinary-load error retirement.
The pending CMO must never reissue, accept younger instructions, or block the
cache's independent snoop service.

For WB authorization and scalar/FP integration, run:

```sh
FIXTURES='rv5stage-core rv5stage-core-rv32f rv5stage-core-rv64d rv5stage-data-fault rv5stage-zicboz rv5stage-interrupt rv5stage-wfi' \
  bash tests/backend/run-circt.sh --simulate-only
```

The RV32F/RV64D benches exercise rejected memory dispatch, committed prefetches,
deferred FP and atomic completion, FP stores/loads, CSR flags, and suppression of younger
FP/register/memory effects behind a data fault. Keep these behavioral checks
at the core boundary rather than depending on generated internal signal names.

For Zicboz, keep permission/fault ownership above the cache, and exercise
both XLEN SRAM sequences as well as the one-completion uncached sequence:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/zicboz-test.rhm
FIXTURES='rv5stage-zicboz rv5stage-csr rv5stage-memory-router rv5stage-mmu-replay rv5stage-dcache rv5stage-dcache-rv32 rv5stage-uncached' \
  bash tests/backend/run-circt.sh --simulate-only
```

The scalar fixture covers request rejection/replay, fence drain ordering,
store-class faults with original `rs1` trap values, and CBZE denial. The cache
fixtures cover all byte offsets and complete-line visibility. The FESVR
payload and simulator validation remain owned by
[`sims/DEVELOPING.md`](../../sims/DEVELOPING.md).

Run host checks only when decode, configuration, specialization, or public
elaboration-time validation changes:

```sh
make rv5stage-host-test
```

For datapath, cache, pipeline, and state behavior, run `make rv5stage-test` or
select the narrowest backend fixture. Exercise WB-stage fault classification
or WFI control flow specifically with:

```sh
FIXTURE=rv5stage-data-fault bash tests/backend/run-circt.sh
FIXTURE=rv5stage-wfi bash tests/backend/run-circt.sh
```

The backend test [`DEVELOPING.md`](../../tests/backend/DEVELOPING.md) owns
fixture modes, tool discovery, and artifacts. SoC integration belongs to
[`../../socs/DEVELOPING.md`](../../socs/DEVELOPING.md), and executable target
coverage belongs to [`../../sims/DEVELOPING.md`](../../sims/DEVELOPING.md).
