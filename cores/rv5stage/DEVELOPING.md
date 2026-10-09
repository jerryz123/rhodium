<!-- Guides contributors through RV5Stage implementation ownership, diagrams, and validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RV5Stage

Read the core [README](README.md) for the pipeline, completion, ordering,
system, memory, privileged-state, and deliberate-limit contracts. This guide
owns implementation placement, change sequencing, generated diagrams, and
focused validation.

## Architecture and dependency boundary

RV5Stage may depend on public Rhodium libraries, the pure RISC-V model and RTL
adapter, reusable components and RISC-V mappings under `cores/`, HardFloat, and shared
CHI libraries. It must not import another named core, a backend, examples, or
tests. The parent [`check-boundaries.sh`](../check-boundaries.sh) enforces these
rules plus fetch/predictor placement, decode-column, and cache-package
separation.

Keep the scalar pipeline dependent on the RV5Stage cache protocols rather than
a generic memory transport. Keep I-cache and D-cache attachments independent of
each other. Physical L1D, geometry, and refill/copyback/snoop machinery belong
to [`cores/cache/`](../cache/DEVELOPING.md); this core retains architectural
completion adaptation and instruction/uncached integration.

## Implementation map

| Area | Ownership |
|---|---|
| [`profile.rhm`](profile.rhm) | Immutable RV5Stage extension, MMU, cache, vector, and completion configuration; projects pure `RiscvIsaProfile` and `RiscvHartDescription` values |
| [`udb.rhm`](udb.rhm) | Exact-version UDB extension closure and fixed RV5Stage architectural parameter claims |
| [`rv5stage.rhdl`](rv5stage.rhdl) | Core, MMU, prefetch routing, cache, uncached, and CHI composition |
| [`core.rhdl`](core.rhdl) | Scalar pipeline, forwarding, hazards, commit, and deferred completion |
| [`observation.rhdl`](observation.rhdl) | Versioned passive observation contract identity |
| [`bundles.rhdl`](bundles.rhdl) | Scalar pipeline payloads |
| [`../memory-response.rhdl`](../memory-response.rhdl) | Shared optional-response pairing and LSU result capture; core instruction/control state crosses MEM/WB in parallel |
| [`../cache-prefetch.rhdl`](../cache-prefetch.rhdl) | Reusable best-effort prefetch operation and request types |
| [`../../rhodium/std/plru.rhdl`](../../rhodium/std/plru.rhdl) | Protocol-neutral invalid-first padded tree-PLRU selection and state update |
| [`fetch/DEVELOPING.md`](fetch/DEVELOPING.md) | Fetch protocols, frontend sequencing, instruction assembly, BTB, and RAS |
| [`decode/DEVELOPING.md`](decode/DEVELOPING.md) | Structured integer and FP control generation |
| [`register-file.rhdl`](register-file.rhdl) | Two-read, two-write integer register bank |
| [`vector.rhdl`](vector.rhdl) | WB macro allocation, autonomous vector execution/memory paths, and macro retirement outcomes |
| [`integer-execution.rhdl`](integer-execution.rhdl) | Tagged shared multiply/divide services and separate scheduled/buffered product-return adapters |
| [`memory-arbiter.rhdl`](memory-arbiter.rhdl) | Scalar/vector LSU lookup ownership, store-commit timing, transaction arbitration, and tagged response routing |
| [`data-port-arbiter.rhdl`](data-port-arbiter.rhdl) | Core-first physical core/PTW arbitration, paired L1D index selection, and origin-tagged response routing before PMA/uncached routing |
| [`vector/DEVELOPING.md`](vector/DEVELOPING.md) | Opt-in XLEN-wide Zve/V WB-launched sequencer, vector CSR state, flat register bank, SIMD packing, and LSU ownership |
| [`fp/DEVELOPING.md`](fp/DEVELOPING.md) | Scalar FP payloads, FPR hazards, LSU bridges, and retirement adaptation over shared [`cores/fp/`](../fp/DEVELOPING.md) components |
| [`csr.rhdl`](csr.rhdl) | Projects RV5Stage configuration and decode controls into the shared CSR/trap service; `core.rhdl` owns WB authorization and retirement |
| [`mmu/DEVELOPING.md`](mmu/DEVELOPING.md) | TLBs, demand translation, best-effort prefetch probes, and page-table walking |
| [`instruction-memory-router.rhdl`](instruction-memory-router.rhdl), [`memory-router.rhdl`](memory-router.rhdl), [`uncached-protocol.rhdl`](uncached-protocol.rhdl) | Physical-region routing, data IO-MSHR composition, and the shared uncached protocol |
| [`cache.rhdl`](../cache/geometry.rhdl) | Shared RV5Stage cache geometry and way/lane masks |
| [`chi/DEVELOPING.md`](chi/DEVELOPING.md) | Physical-region/Home policy, RN identity, cache transaction engines, and the shared uncached RN-I implementation |
| [`icache/DEVELOPING.md`](icache/DEVELOPING.md), [`../cache/DEVELOPING.md`](../cache/DEVELOPING.md) | Named fetch metadata and shared physical instruction/data caches |
| [`memory-context.rhdl`](memory-context.rhdl) | Architectural destination and core/PTW ownership, opaque to shared services |
| [`tests/`](tests/) | Decode, configuration, public specialization, and invalid-use checks |

## Change the core

Select active or disabled FP/vector services at instantiation. The vector
pipeline's disabled implementation owns inactive service outputs; scalar WB
launch qualification, FPR reservation, and retained retirement context remain
in `core.rhdl` and use the same connections for either specialization. Preserve
the active vector instance's passive observation boundary when changing this
selection. FP calendar/arbitration policy belongs in `fp/service.rhdl`, not in
the reusable numeric service.

Follow the [source documentation requirements](../../AGENTS.md#source-documentation),
including the exemption for files under `tests/`.

The optional simulation-owned cosim adapter captures real CSR interrupt/time inputs and the drained
interrupt-check boundary at the same edge as WB observations. These are passive
taps, never simulator-driven core inputs. Preserve the environment at allocation
across delayed completions. Sail owns deterministic CSR evolution; do not add
CSR-bank snapshots merely for comparison. Cycle timing comes from reset-relative active
edges. The simulation owner documents checking policies in
[`sims/cosim/README.md`](../../sims/cosim/README.md).
`observation.rhdl` owns `rv5stage.v1`. `core.rhdl`
declares that identity, detached configuration, and semantic taps; it imports no
DPI capture implementation. Capture lives under `sims/cosim/rv5stage/` and is
selected by the compilation pass. Adapter eligibility belongs there, not in the
functional core's metadata.
All observer identities, queues, and completion tables
belong to the native `sims/cosim/rv5stage/adapter.*`. Keep the scalar, FP, and
vector capture RTL stateless; the adapter supplies vector WB identities before
resolving its per-cycle events. Reset epochs come from the host collector, not
from a DPI-result register in the observed design.

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
   transactions, device reads, mutations, FP architectural effects, hints, and reservations.
   A speculative lookup must never allocate, mutate, reserve a destination, or
   start device IO. Rejection replays before transaction acceptance; accepted
   transactions must never be replayed. Keep coherence service independent.
   Vector instructions likewise pass ID without predicting descriptor capacity.
   WB atomically admits the descriptor and any required floating-point
   scalar-result reservation, or precisely replays the instruction without
   vector, precheck, or reservation side effects. Keep dependency, certification, memory-ordering, and
   serialization interlocks in ID; descriptor availability is not an ID hazard.
   State observers wait for older vector launches in EX/MEM/WB as well as
   admitted vector work; the vector sink's registered active flag alone leaves
   an admission gap before effects such as accrued FP flags become visible.
   Deferred GPR/FPR dependencies and scalar memory ordering likewise cover
   those pre-admission tokens; speculative capacity admission must not bypass
   a dependency merely because its WB scoreboard reservation has not happened
   yet. Vector memory launches instead retain program order through their
   descriptor FIFO and sequencer, without waiting in ID for an older vector
   store to drain.
   Independent younger work may pass ID behind a vector memory launch.
   EX adds the snapshot `vstart` byte offset with the scalar ALU; MEM uses
   the ordinary DTLB read for a speculative one-page certificate. WB validates
   and enqueues the captured mapping with the descriptor, retiring the macro
   independently of the older vector window. Issued requests carry the physical
   mapping into a downstream retry owner, without acquiring that window.
   If no certificate was captured, WB restarts
   younger work before any of it reaches WB. A slower admitted macro restores the
   certification barrier. A younger vector launch still waits for an older
   launch to clear nonzero `vstart` before legality checking.
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
   Do not gate forwarding with live MEM fault/replay/kill results;
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
   `mem_wb_pipe` stores `MemoryWritebackContext`, the instruction/control projection
   of `MemoryWriteback`, including the ordinary ALU value but not `load_hit` or
   `cache_outcome`. `memory_response` stores the selected LSU outcome and data
   independently on the same edge. A combinational join reconstructs the flat
   WB payload and selects load-hit data. Both payload captures remain
   unconditional: a squash cancels validity, not forwarding payload capture.
   Never replace the registered load-data source with a live MEM response.
5. Test cycle-visible behavior in the narrowest rsim fixture, then
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
Architectural restart is the one exception to old-epoch reservation gating: it
offers the restart target in the recovery cycle, and a transfer atomically
replaces the flushed S1 context. The MMU and L1I must preserve the same
flush-plus-transfer epoch rule. If any downstream stage is not ready, retain
the target and retry it instead of claiming an S0 occurrence.
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

The S2 scanner keeps its own partial instruction and first halfword for fetch
lookahead, independently of the core's residual parcel. It validates branch
locations and lengths without full compressed expansion. On a BTB miss it also
recognizes direct `JAL`, `C.J`, RV32 `C.JAL`, and return hints, including
straddling 32-bit instructions. It computes direct targets from the immediate
and uses a nonempty RAS for returns. An accepted fallback truncates trailing
parcels, redirects only younger S1 work, and discovers an unconditional BTB
entry. It never skips an earlier control-flow instruction. A stale cut is
removed from the current packet; a registered repair invalidates the BTB entry
and restarts after that packet, preserving older queued instructions.
Architectural recovery wins.
A source clear without restart stops admission until an explicit restart; never
reconstruct recovery from the core's assembly cursor or speculative next PC.

Frontend S1/S2 and the repair pipeline use flushable Valid pipes with explicit
`flush` inputs. Recovery clears old validity at the edge while preserving the
global reset domain and always-capture payload timing. A transferred restart
replaces S1 rather than clearing it; S2 remains unconditionally cleared. Retain
the existing same-cycle output filters so old pre-edge transfers cannot escape.
The intrinsic pipe contract exposes flush or replacement to event lineage
instrumentation.

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
recovery as distinct payload fields. The 32-entry BTB compares 14 low address
bits per entry and shares eight upper-address pages between source PCs and
targets. Reusing a page invalidates every dependent entry so truncated storage
never creates a false full-address hit or reconstructs a stale target. Keep the
page CAM and low-entry comparisons parallel in S1. The BTB is ordinary
named-core RTL, not a new language feature or ISA profile parameter.

The RAS is also named-core RTL. Its default six-entry speculative stack drives
return targets for S1 BTB hits and fault-free S2 predecode fallbacks; only an
accepted, validated S2 prediction applies its action. The registered late
control-flow fallback is a soft redirect: it preserves older queued packets and
the accepted instruction while killing younger S1/S2 work. Its BTB discovery
is distinct from resolved branch training and never advances resolved RAS
state. A second
resolved stack advances from uncancelled S4 outcomes. An
ordinary architectural recovery restores speculative state from the resolved
state, including the resolving action when present, while an action mismatch
repairs the stack even when the target itself happened to match. Predictor flush
clears both copies. Classify expanded `JAL` and `JALR` instructions plus raw
compressed `C.JAL`, `C.JR`, and `C.JALR` from the RISC-V `x1`/`x5` implicit hint
table, preserving distinct push, pop, and pop-then-push actions. Do not infer
calls from arbitrary nonzero link registers.

For predictor changes run the shared `bpred-btb`, `bpred-ras`,
`rv5stage-fetch-prediction`, `rv5stage-return-prediction`, and
`rv5stage-branch-prediction`, then existing `rv5stage-fetch`, `rv5stage-core`,
and fault/replay fixtures. The paired branch core test compares actual stores,
cycle counts, flushes, and consecutive backedge requests with prediction enabled
and disabled; the paired return test holds the BTB constant while alternating
two call sites into one return with the RAS enabled and disabled. Neither test
inspects internal predictor state. The fetch test covers compressed branch
ordering, continuation words, duplicate-PC occurrences, backpressure, stale-cut
repair, and precise continuation faults.

Pipelined scalar multiply reserves the following EX launch and its GPR return
cycle atomically at ID admission. The GPR delay is multiplier latency plus the
ID/EX boundary. EX sends operands directly into the three-stage multiplier;
there are no scalar request queues. A registered launch grant prevents vector
work from displacing an admitted scalar. A waiting vector request gets a turn
by withholding new scalar admissions, never by delaying a promised launch.
Independent scalar multiplies may enter EX on consecutive cycles.
WB authorizes a surviving scalar multiply two cycles after EX, while reserving
its architectural GPR destination in the scoreboard. Delay this authorization
through the remaining arithmetic stages and discard results without it. Timing
identifies the scalar owner, so no ticket allocator or cancellation table is
needed. Pipeline recovery must not flush authorization for committed older work.
Iterative multiply and divide still enqueue from authorized WB commit, with
operand payloads from the normal WB pipeline token. Retained CMO and WRS
contexts must not select arithmetic operands. Their scalar adapters retain a
one-entry request queue reserved in ID; the iterative multiplier also retains
its scalar staging queue and vector-first arbiter. The divider retains its
round-robin arbiter and two-entry service queue. Standalone elastic wrappers
remain available for callers that do not promise immediate result consumption.
The vector multiplier request queue retains operands from accepted feed-forward
beats during shared-service contention and bypasses when empty. Result tags
retain selection and destination metadata until consumption.

`../writeback-calendar.rhdl` owns future physical write-cycle reservations. The core
reserves the deferred GPR port for pipelined multiplication, fixed FP integer
returns, and vector-to-integer movement before launching each fixed operation.
The vector composition independently reserves the VRF port. Shared-service
launch and every applicable reservation are one atomic transfer for vector/FP
work. Scalar pipelined multiply instead books its fixed future launch and GPR
return at ID admission; a squashed grant may expire unused. Same-cycle WB
reservations precede ID admission, so younger work cannot feed back into older
WB readiness. A variable
response waits at its producer; an aged waiter pauses new reservations, without
revoking already-issued work. The broader full-SoC and CoreMark validation of
scheduled writeback predates the direct EX-admission refinement; rerun it before
claiming that refinement has the same coverage. The `rv5stage-multiply` trace
checks three cycles from producer EX to dependent ID admission and consecutive
independent launches. `rv5stage-integer-execution` checks exact three-cycle
returns, WB authorization, cancellation, and reset through the production scalar
adapter; `rv5stage-vector-muldiv` covers shared scalar/vector execution.

Fixed FP similarly reserves EX service ownership and its selected return cycle
at ID. `cores/fp/timing.rhdl` is the common timing contract for execution, scalar
authorization, and vector VRF scheduling. Results earlier than WB are aligned
to WB; longer results carry delayed authorization, not a generic result buffer.
Only authorized results set/clear architectural destinations or accrue flags.
FP-to-GPR results at WB use the ordinary port, while later results reserve the
deferred port. Divide/sqrt remains WB-launched and retains its terminal result.
ID avoids promising EX FPR reads over older WB division or vector snapshots.
An EX reservation takes priority over vector launch, with admission gaps giving
waiting vector work a turn. Store probes retain their independent ID/EX timing.

## Pointer-masking ownership

The opt-in RV64 Ssnpm path uses reusable policy and address helpers from
`riscv/rtl/pointer-masking.rhdl`. The shared `cores/csr/file.rhdl` owns PMM state and WARL writes,
and reexports the shared `PrivilegeMode` for existing core consumers.
ID captures `PointerMaskControl` in `DecodeExecute`; EX transforms only the
effective memory address and leaves the integer result and low 48 bits intact.
The transformed address feeds speculative lookup, registered WB requests,
explicit prefetches, replay correlation, and address-fault values. Fetch,
branch targets, implicit PTE requests, and CSR writes never pass through it.

With H enabled, the shared helper resolves ordinary effective VS/VU contexts,
MPRV/MPV, and explicit HLV/HSV independently. CSR exposes ordinary and explicit
guest controls; ID selects by decode and disables HLVX masking. Keep the ordinary
output independent of live ID intent because WB vector certification consumes
it too. `henvcfg.PMM` and `hstatus.HUPMM` use the same WARL modes as senvcfg.
HS and VS MXR both suppress guest masking. Vector descriptors capture the control,
and WB rejects mismatching early certificates. Existing CSR drain closes older
vector windows before policy changes; PMM never changes PTE interpretation.
Use the host/guest policy sweep and `riscv-hypervisor-csr` plus
`rv5stage-hypervisor-core` for WARL, restart, nested memory and fault coverage.

CSR serialization keeps policy stable across younger ID admissions. A committed
PMM change uses the ordinary serializing restart, killing younger work and
clearing the MMU's prefetch stages through the fetch-flush path. It does not
assert `translation_flush`: PMM changes neither PTEs nor translation tags.
Accepted older memory operations drain before the CSR commits. Ordinary trap
and return redirects continue to establish the next instruction's context.

Keep the hardware and execution-environment selections distinct: `ssnpm` owns
the core mechanism, while `supm` is legal only with `ssnpm` and publishes the
validated user-environment contract. The generic defaults remain disabled;
`SingleCoreRV5StageSoC` is the validated concrete profile that enables both.
Run `pointer-masking-test.rhm`, `profile-test.rhm`, `riscv-pointer-masking`,
`rv5stage-pointer-masking`, and `riscv-csr`; include `rv5stage-mmu-replay`
when modifying the shared effective-data-privilege helper. The core fixture
checks policy changes, tagged payload preservation, replay, all prefetch kinds,
and transformed access-fault trap values without relying on internal nets.

## Pipeline event annotations

`rv5stage.rhdl` declares `trace_instance("hart", hart_id)` once for the entire
core/cache/MMU subtree. Keep per-event labels independent of hart identity;
the event compiler samples the stable hardware ID and the exporter supplies
the hierarchy group. Standalone component traces remain unscoped unless their
caller supplies context.

The Flow stage modules own their public interface trace contracts; `core.rhdl`
does not redeclare them on instances. Keep storage certification local to its
implementation; do not replace
always-capture payload registers or derive controls from generated signal names.
EX's payload is still computed unconditionally; its flow filter gates only
token validity, preserving the feed-forward datapath and cancellation timing.

The inline retirement flows in `core.rhdl` preserve WRS-over-maintenance-over-live WB payload
selection, even before a resident completes and while the output is invalid.
Generic `OfferRegister` instances own pending payloads and their intrinsic
lineage contracts. Capture comes from the original MEM/WB token;
completion gates acceptance and releases that same owner. The retained memory owner
also holds misaligned scalar accesses while the MMU sequences independent
fragments. Admission squashes younger work; success restarts at the next PC,
and fault enters the trap path with the retained instruction and fragment VA.
No split load reserves a deferred destination: successful completion uses normal
WB or the FPR load-hit port. Interrupts/context changes wait for this owner.
Keep WRS timeout/wake policy, maintenance
completion/fault policy, and architectural commit gating in `core.rhdl`.
Grant selection follows pending ownership, then gates completion; arbitration
must not fall through to younger live WB while a resident is unfinished.
Explicit forks separate WB arrival, memory observation, FP issue, and retirement
consumers. The final WB observation branches from selected commit and uses
`csr.retired`, the same success condition as `minstret`. Replay and trapping
tokens have no WB event; memory observations and restart flows retain their own
MEM ancestry independently of that observation. CSR trap/return and retained
exception redirects remain a separate unmodeled boundary.
Vector-memory retirement likewise retains its accepted instruction in an
`OfferRegister`, released by the existing vector outcome. That outcome supplies
fault fields, while the stored instruction supplies the retirement ancestry.
`RV5StageBranchPrediction` stores only the effective-next-PC comparison, computed
in EX and carried through ExecuteMemory/MemoryWriteback and retained contexts.
Keep RAS-action mismatch out of this accuracy metric. Reuse the resolved and
predicted actions already carried in ExecuteMemory's `branch_update`, comparing
them into MemoryWriteback's `ras_mismatch` for live and retained retirement.
Do not gate it by branch classification, because a stale
prediction can request a stack action on a nonbranch. Neither capture changes recovery.
The `rv5stage-retirement-trace` fixture drives public packet predictions and memory
completion controls, checking exact retired order, compressed and indirect
targets, independent next-PC/RAS mismatches, same-PC reissue, squash, CSR/data
traps, delayed WRS/CMO, and MEM ancestry.
Run `event-offer-register` for retained ownership, replacement, stalls, and reset;
use `rv5stage-core`, `rv5stage-zicbom`, `rv5stage-zawrs`, and the FP core fixtures
for production retirement selection and completion policy, then the SingleCoreRV5StageSoC
trace check for integration.

`fetch/source.rhdl` maps restart, late redirect, replay, and S1 successor occurrences into
candidate flows, then selects restart over late redirect over replay over S1 over the saved cursor.
`RV5StageFetchCursor` retains the last update with a local storage contract;
an explicit unannotated reset seed supplies the initial fallback. Cursor update
priority is separately restart/late redirect/clear, accepted advancement, replay, then S1.
Clear preserves the PC while stopping admission; restart replaces it even when
that same-cycle offer transfers. Preserve inactive continuation-target payloads.
`RV5StageFetchOffer` keeps admission independent of candidate validity, asserting
that the fallback supplies a candidate. Do not replace it with a gate whose
validity depends on replay, or insert a cycle before a restart can transfer.
Only reset seeds and unmodeled upstream control owners remain unknown.
The intrinsic flushable pipes connect S0 through `frontend/s1.lookup` and
`frontend/s2.outcome`, which captures replay, admission, and admitted fault flags.
Only admitted outcomes pass the Flow filter into packet storage; no
additional checkpoint represents that same-cycle admission. ShiftQueue owns
its shifting-window contract and explicit flush. Core-side assembly declares
a depth-one window using actual residual capture/release, clear, and independent
resident/live contribution predicates. A word can parent two compressed
instructions; a straddle has two word parents, including a faulting continuation.
`core/s2.decode` inherits those
parents instead of cutting ancestry. Frontend replay attempts are new occurrences
parented by the failed S2 outcome; selected modeled core restarts inherit their
pipeline occurrence. Sequential/predicted successors inherit the preceding S0
through S1, and blocked candidates retain their selected cause.
I-cache TXREQ inherits the S0 occurrence that launched its refill. Packet scanning
explicitly forks admitted outcomes into packet delivery and prediction repair;
the repair pipe retains the cause until its restart is selected. MMU walk,
predictor-training, and unmodeled control ownership remain separate.
Run `rv5stage-fetch-source`, `event-window`, `event-frontend`, `rv5stage-fetch-prediction`, and
the host admission test for changes at this boundary.
Later checkpoints must not become independent roots to hide an unsupported
path. Decode transfers fire only when the hazard gate admits them,
but its checkpoint must precede `gate_flow` and follow the squash filter so
stall observations see valid instructions while issue is blocked. Keep the
captured Boolean reason terms aligned with `pipeline_hazard`; do not impose
priority on simultaneous reasons. Keep the unannotated WB arrival token distinct
from the retirement checkpoint and deferred register completion.

Keep `dcache/s1.access` inside L1D on physical lookup resolution, before the
response/store-candidate fork. It is shared by scalar and vector requesters;
capture cache fields, not instruction metadata the cache does not own.
Fork scalar EX into instruction context and lookup flows. The shared LSU
arbiter routes returned lineage only to the selected requester; losing lookups
produce local replay from their own retained context. MMU similarly chooses
cache-return lineage or its local translation/fault outcome. Preserve inactive
payload values and all original gating predicates.
At scalar MEM and vector decision capture, pair context with the optional
same-cycle response using a combinational Flow join. A context-derived fallback
supplies the absent response, so the join cannot wait, drop a context, or add
storage. Filter responses for killed contexts before the checked conversion.
WB inherits MEM and available cache ancestry through the parallel core and LSU captures;
do not override its parents to discard either contribution.
`dcache/s2.resp` observes the scalar response at WB before slow-request gating.
It retains those same parents independently of retirement; replay/fault responses
must not depend on a non-fired WB checkpoint.
Its observer runs after the two captures rejoin, where architectural fault,
replay, and admission policy is available; the response storage belongs to
`MemoryResponseCapture`, not the core instruction register. Its display group
is `dcache`; do not add a duplicate core result event.
`vector/memory.result` observes the adapter's registered decision. Gate
memory observations without filtering functional tokens. Hits, faults, and
replays remain visible even without cache access. Record nonfaulting admission
without feeding readiness/fault status into functional request validity.
The cache's named queue, S3 elastic pipe, and S4 always-capture register carry
their own trace models. Observe S3 before its advance gate (including stalls)
and S4 before its hit/transaction demux. Capture direct refill acceptance and
command fields on S4, before arbitration with post-eviction commands; do not
certify the gather FSM as a pipe.
Walker and admitted prefetch checkpoints infer available ancestry just like
demand checkpoints; unmodeled source state remains unknown in partial mode.
`rv5stage-load-hit` instruments the actual core/MMU/router/cache composition;
its DPI scoreboard checks EX-to-cache timing, independent MEM/cache parents at
WB, same-cycle caller capture, public admission against result fields, FIFO ancestry through
S3/S4, exact miss PCs and direct refill addresses, and S4 ownership across
backpressured CHI attempts, RetryAck, and PCrdGrant. Refill owns the scoped
command-to-attempt contract before its Flow request mapper; unmodeled engine
branches retain unknown ancestry through request arbitration in partial mode.

After edits, run `rv5stage-core` for forwarding, stalls, replay, redirects, and
deferred completion, then the SingleCoreRV5StageSoC trace smoke. Its native Perfetto checks
follow exact occurrence edges, compare named PC/instruction captures, and
check one-cycle or elastic delays without requiring every fetched
token to survive. The conversion/fanout compiler fixture covers both Valid
replication outputs, including a dropping branch.

Keep private-cache outer-channel observations in `rv5stage.rhdl` at the
L1I/L1D-to-CHI composition boundary. The local connection helper preserves all
available ready-valid channel directions and captures only named scalar metadata.
Select each channel's enum opcode with `~format: "enum", ~label: #true`; do not
hand-maintain REQ/RSP/DAT/SNP decoding tables in the exporter. Check decoded slice
names and symbolic opcode arguments against the schema's enum table in the trace smoke.
Do not infer CHI transaction ownership by matching TxnID/DBID values. Request
checkpoints supply occurrence identities that Flow carries through network transit.
I/D-cache incoming RSP/DAT observations inherit certified Home output ancestry;
the inclusive Home's retained request scope bridges its FSM. Outgoing RSP/DAT
and snoops also infer all available parents, reporting unmodeled owners as gaps.
The line engines' [CompAck contract](chi/README.md#cache-line-refill) connects
the line-completing RXDAT packet to its acknowledgement. Refill completion
and backing-memory provenance remain separate.
Enable stall companions without requiring activity on idle channels.
After changing them, run the SingleCoreRV5StageSoC trace smoke; its
`check-cache-events.sql` checks schemas, real miss/refill traffic, endpoint IDs,
and the lack of fabricated parent edges. Instruction RN-I has no SNP channel;
only the data cache contributes snoop events. Use a cache-heavy benchmark to inspect
additional writeback/snoop activity; an idle channel need not emit an event.
`check-stall-events.sql` separately validates admitted packet ancestry for Decode
stalls and reason flags without counting observers as transfer fanout.

## Maintain the UDB projection

Keep selectable extension membership derived from `RV5StageConfig`. Keep fixed
CSR, trap, alignment, counter, PMP, and LR/SC facts in `udb.rhm`, and pass
physical address width and PMA granularity from the integration boundary. When
one of those behaviors changes, update its RTL owner and UDB claim together.
`AMO_MISALIGNED_BEHAVIOR` explicitly preserves the core's alignment exception
independently of scalar misalignment support. The ACT UDB overlay declares
this policy because the pinned database lacks an AMO fault-selection parameter.

Run the pure UDB encoder and RV5Stage projection tests, generate a concrete
configuration, and validate it with the UDB version pinned by the ACT4 checkout:

```sh
tools/run-racket-tests.sh riscv/tests/udb-test.rhm cores/rv5stage/tests/udb-test.rhm
make riscv-udb-config RISCV_UDB_CONFIGURATION=simple-rv5stage-rva23
bundle exec --gemfile sw/riscv-arch-test/framework/src/act/data/Gemfile \
  udb validate cfg /tmp/rhodium-udb/simple-rv5stage-rva23.yaml --custom sims/arch-test/udb-overlay
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
tools/run-racket.sh -S "$PWD" tools/write-rv5stage-core-diagram.rhm \
  /tmp/rv5stage-core-diagram
```

The source is
[`../../examples/rv5stage/core-diagram.rhdl`](../../examples/rv5stage/core-diagram.rhdl).
The JSON targets interactive renderers; the compact DOT view links child
modules by name instead of flattening them.

## Focused validation

Standalone register-file, FP pipeline, integer execution, writeback calendar,
memory arbiter, divider workload, and vector-sequencer fixtures drive emitted
rsim C++ directly. Their sources and independent scoreboards live under
`tests/rsim/` and run in the `cores-components` CI group. Select fixtures by name:

```sh
python3 tools/testing/rsim/run.py --fixture rv5stage-integer-execution --fixture rv5stage-writeback
python3 tools/testing/rsim/run.py --fixture rv5stage-vector-sequencer --fixture rv5stage-vector-sequencer-rv32 --fixture rv5stage-vector-sequencer-1024
```

Keep reset, pre-edge handshakes, stalls, cancellation, and parameter sweeps in
the C++ oracle. Composed core workloads, instruction prediction/buffering,
interrupt/privilege control, vector execution, memory routing, coherent caches,
and LR/SC progress also run directly through rsim. Their groups are
`cores-execution-frontend`, `cores-execution-control`, `cores-execution-datapath`,
`cores-vector-functional-1`, `cores-vector-functional-2`,
`cores-vector-configurations`, `cores-memory`, and `cores-cache`.
The 128-bit `rv5stage-vector-reduction` fixture owns the full SEW/LMUL,
aliasing, and empty-operation sweeps. `rv5stage-vector-mask-512` keeps the
wide-mask boundaries at bits 63/64 and 511/512, scalar counts of 512,
SEW8 prefix/index wrap, and representative multiword reductions and
permutations with stalls, cancellation, and restart. Do not duplicate the
full parameter sweep at the larger width.
`tests/rsim/driver.hpp` settles host combinational responses, samples pre-edge
transfers, ticks the DUT, and then publishes registered host updates. Keep this
ordering when porting a scoreboard; a native record must be assigned by fields,
not initialized from its former packed SV integer.

Alternate vector fixtures cover parameter boundaries, not a cross-product of
every subsystem with every supported value. Keep one behavioral owner for each
distinct risk: RV32 packed transport, RV32 sequencer geometry, a single
completion slot, wide mask indexing, and gather indices above 255. The default
functional fixtures own operation breadth. A second fixture should not rerun a
complete operation scoreboard merely to repeat a shared completion-slot or
XLEN value already covered at its owning boundary.

Fetch/source/prediction/throughput, multiply, retirement, and load-hit trace
fixtures run directly on rsim with their independent lineage and timing oracles.
The RV32F/RV64D complete-core fixtures share `tests/rsim/core-wb-behavior.hpp`;
vector-memory and one-slot configurations share `tests/rsim/vector-memory-behavior.hpp`.
Keep the same complete workload in each parameter configuration.

[`tests/rsim/port-observers.rhm`](tests/rsim/port-observers.rhm) is test-only
instrumentation replacing the former SV bind/hierarchical monitors. It copies
the prepared hierarchy and adds clocked callbacks at the same public component
ports, without changing functional ports, state, or production generators.
Each observer requires one matching module definition and explicit scalar field
paths; callbacks receive pre-edge values before deferred host responses publish.
The FP oracle proves a killed operation launched and returned without WB
authorization. Vector oracles check exact VRF row/data/mask writes, contiguous
write timing, scalar overlap, and fast-certificate acceptance. Keep these checks
when changing a fixture; final architectural signatures alone do not replace them.
Vector configuration composes this observation pass before event tracing and
retains its independent ancestry oracle. Co-sim retains its separate HDL checks.

`rv5stage-vector-config` additionally instruments its existing real-core
program and checks each sequencing occurrence against its scalar WB ancestor,
while retaining its architectural signatures and exact VRF-write scoreboard.
The `rv5stage-load-hit` fixture also links RHEG and exports a matching descriptor;
its scoreboard checks D-cache S1/MEM and S2/WB alignment, public core/cache
admission, and retained S2-to-S3-to-S4 ancestry including direct-refill fields.
The existing bench checks functional load timing and architectural results.
Every refill receives RetryAck and PCrdGrant before retransmission, with request
backpressure; both attempts must retain the same refill residency, whose parent
is the original S4 occurrence.
`rv5stage-retirement-trace` drives the real core's public fetch packets with
explicit predictions and delayed memory responses. Its DPI scoreboard checks
exact retired PCs/instructions/prediction fields and original MEM parents, with
compressed branches, indirect targets, RAS-only repair, replay, squash, traps,
and WRS/CMO retention. It can save a collector snapshot through
`RHEG_RETIREMENT_SNAPSHOT` for native Perfetto validation.
`rv5stage-load-hit` can similarly save its shared-cache ancestry snapshot with
`RHEG_LOAD_HIT_SNAPSHOT`.
Scalar architectural observation uses `rv5stage-cosim` (RV64, pipelined multiply)
and `rv5stage-cosim32` (RV32, iterative multiply). Both drive real observed and
unobserved occurrences of the same core definition in lockstep; the compilation
pass selects only the observed occurrence. They use the production C++ collector to validate
ordered records through delayed load/multiply/divide, x0, replay, load/store
hits, LR/SC/AMO results, CSR WARL, precise traps and returns, interrupt entry,
retained WRS/maintenance, and reset abandonment:

```sh
FIXTURES='rv5stage-cosim rv5stage-cosim32' bash tools/testing/circt/run.sh --simulate-only
```

Observe semantic acceptance/completion events, not trace metadata. Per-service
owner queues rely on the ordered authorized scalar memory service and ordered
scalar arithmetic responses; assert the original response tag at removal.
FP compute uses a destination-indexed FPR owner table because fixed and variable
responses can reorder, plus a FIFO for fixed integer results including x0.
The FP admission event is WB authorization, never speculative EX launch. A
fixed completion coinciding with its own authorization uses that WB owner
directly; an older queued owner still takes precedence on replacement edges.
FP load writes retain the memory owner separately. Check these invariants with
the native adapter tests; use existing software suites with `COSIM=1` for
instruction coverage rather than a separate FP qualification payload.
Queues must never supply functional readiness. Sample CSR contributions after
the owning edge and before the next edge; exclude autonomous counter and input
changes. The generic hook/collector contract belongs to
[`sims/cosim/`](../../sims/cosim/README.md); transport changes also require
`cosim-hooks` and `make -C sims cosim-hooks-test`.

Svinval's pure catalog is `riscv/isa/svinval.rhm`. The optional decode rows
reuse the corresponding SFENCE/HFENCE actions, with no register sources because
the MMU intentionally over-invalidates all entries. Keep the ordering-only
`TranslationOrder` action distinct: it serializes without an MMU flush and
must not inherit TVM/VTVM denial. Its U/VU privilege check remains in CSR
commit policy; no speculative or denied instruction can invalidate state.
Run `riscv/tests/svinval-test.rhm`, `cores/rv5stage/tests/core-ctrl-test.rhm`,
and `cores/rv5stage/tests/profile-test.rhm` through `tools/run-racket-tests.sh`.
The `riscv-hypervisor-csr` fixture sweeps all five operations across
M/HS/U/VS/VU and independent TVM/VTVM settings. `rv5stage-hypervisor-core`
checks paged VS/G remapping after delayed PTE stores, batched invalidations,
and precise guest denial without younger stores. Pair with
`riscv-guest-translation` for invalidation on refill and accepted-response
edges and orphan-response draining, and `riscv-csr` for ordinary privilege
regressions. Svinval selection does not itself advertise H, Sha, or RVA23.

State-enable descriptors live in `riscv/isa/csr.rhm`; the stateless hierarchy
and denial priority live in `riscv/rtl/state-enable.rhdl`. The shared CSR bank generates
four descriptor-driven banks with optional M/H storage. Only SE and ENVCFG
are writable for the current core; S-state views are shared constant-zero CSRs,
not virtualized duplicates. Parent-masked H bits ignore writes. Bank writes
use the same successful WB-commit predicate as all other CSR mutation.
When adding state controlled by another state-enable field, extend the legal
mask and access policy together; do not enable fields for absent state.
Use the existing hypervisor CSR fixture for independent SE gates, parent masks,
M/HS/VS/VU denial and compatibility with FP/vector/timer controls. Its reset
helper defaults to explicit firmware initialization; raw-reset tests opt out.
The RV32 Sstc fixture also covers state-enable high halves and S-mode denial.
The hypervisor core fixture executes 48 paged read/write cases, including
precise fault PCs/values and suppression of younger stores. Its common machine
bootstrap explicitly initializes state access for preexisting guest programs.

Sstc's full-width compares and privilege-gate classification use
`riscv/rtl/timer.rhdl`; comparator storage and CSR writes live in `cores/csr/file.rhdl`.
Keep its optional state absent from non-Sstc specializations. RV32 low/high
writes preserve the other half. Never use V-gated `time` CSR readback as the
guest comparator input: virtual time advances while HS/M executes too.
STCE switches pending-bit ownership, not just access permissions; an enabled
host comparator replaces physical/software STIP, whereas guest comparison
ORs with injection. Machine denial precedes guest virtual-instruction denial.
The existing WB drain and interrupt boundary remain the only entry authority.
Run `riscv-sstc-rv32`, `riscv-hypervisor-csr`, and
`rv5stage-hypervisor-core`, plus ordinary `riscv-csr`. The guest core cases
exercise comparator delivery during delayed loads, WFI/rearm/SRET, and vector
completion drain. Profile and CSR catalog changes use the focused host tests.

The hypervisor integration is selected by `RV5StageExtensions.hypervisor`.
Core, frontend, instruction assembly, CSR, and top-level composition consume
that profile; standalone MMU and protocol generators retain explicit H
parameters. Configuration validation requires RV64 and Sv39. The shared RVA23
preset enables H/Sha and its state-enable dependency for both core choices.
Decode composes canonical HFENCE and HLV/HLVX/HSV rows into the existing relation.
WB's ordinary serialization drains older work before committing fences.
Conditional state fields avoid H storage in normal specializations. Address
substitution selects the VS bank while access checks retain the original
instruction's CSR address. Commit-owned trap/return events are the only
writers of live virtualization state.

Runtime `misa.H` comes from the profile's MISA projection, using `riscv/isa/profile.rhm`'s shared
bit catalog. ACT projects the same advertised profile into Sail; inventory gaps
and unconfigurable model differences remain visible, not suite exclusions.
The hypervisor CSR bench sweeps WARL translation modes, direct VS vector bases,
counter enables, and delegated trap values. The core fixture enables C and
checks page-straddled fetch faults, raw illegal instructions, load/store faults,
VS delegation, and precise younger-store suppression. Pair changes to these paths with the
ordinary CSR regression; use profile host tests for MISA catalog changes.

`henvcfg` storage is conditional on H and uses the same FIOM/CMO WARL masks
as the existing environment CSRs, plus opt-in PBMTE. PBMTE readback and VS
translation are masked by machine PBMTE; both enable changes conservatively
invalidate the MMU at WB. Unsupported extension fields stay zero.
Do not mask CMO readback by an ancestor's enable bits. The reusable CMO adapter
returns `CboAccess` (allowed, illegal, virtual) plus the effective management
operation. Decode captures the decision using current privilege and V, not
MPRV translation privilege; the normal exception token carries it to WB.
No denied CMO can issue a cache request. `senvcfg` remains shared. Existing
full-drain FENCE serialization is stronger than FIOM requires. Device regions
cannot be cacheable, and the uncached IO-MSHR rejects atomic accesses, so no
FIOM-specific request bit or new queue is needed.
Use `riscv-cmo` for exhaustive policy checks, `riscv-hypervisor-csr` for
WARL/access/readback, and `rv5stage-hypervisor-core` for paged CMO trap priority,
WB acceptance, invalidate conversion, and delayed CBO/FENCE ordering.

Guest FP shares the ordinary FPRs and `fcsr`; the conditional `guest_fp_status`
field independently owns VS.FS while `fp_csrs` retains HS.FS. Keep both outside
raw trap-stack status writes, and derive SD independently for each bank.
Status writes select the substituted CSR address, so guest `sstatus` cannot
clean HS.FS. Guest FP writes/completions dirty both banks. Do not duplicate
the FP datapath or bank flags. Existing ID serialization, deferred scoreboards,
and WB exception/interrupt drain keep live V stable until every accepted FP
completion updates its owning context; a core assertion guards trap/return
against undrained FP work. Keep this invariant when changing completion policy.
The guest CSR fixture covers all FS combinations, shared flags, independent
SD, and VS/VU access. The paged core fixture includes F/D arithmetic, dynamic
rounding, two-stage FP memory faults, deferred divide/fault and interrupt
ordering, and SRET with shared FPR state. Pair it with `rv5stage-core-rv64d` and
`rv5stage-core-rv32f` for ordinary FP integration.

The H specialization owns only `hvip` and `hideleg` as new interrupt storage.
M/H/VS enable views share `mie`; pending views alias `hvip` and physical sources.
Keep HS `sip`/`sie` disjoint from the virtual-interrupt bits, and keep virtual
MIDELEG bits fixed one. `riscv/rtl/interrupt.rhdl` owns stateless destination,
priority, enable, and cause-renumbering policy. The existing interrupt request
stops younger admission and WB drains accepted effects before entry; never
flush an accepted load just because an injected interrupt becomes eligible.
`riscv-hypervisor-csr` checks CSR aliases and M/HS/VS selection;
`rv5stage-hypervisor-core` runs all three injected interrupts through paged
guest handlers, WFI wake, deferred loads, and SRET. Pair these with ordinary
`riscv-csr`, `rv5stage-interrupt`, and `rv5stage-wfi` regressions.

Fetch result/packet and scalar pipeline bundle generators take an explicit
`hypervisor` argument. The false specialization omits guest metadata and its
storage. Fence controls gain a bit for the two HFENCE actions in both
specializations. The true specialization carries `RiscvGuestException` through S2,
packet storage, instruction assembly, and ID/EX/MEM/WB. The MMU's
`pipeline_guest_fault` is paired with its same-cycle MEM response;
`request_guest_fault` is paired with the WB request fault indications. Capture
the former in MEM/WB and the latter before pending-exception retention. Neither
is an independently sampled global fault register. Physical cache interfaces
stay unchanged.

Guest vectors retain the same sequencer, VRF and shared execution services.
`guest_vector_status` owns VS.VS independently of `vector_csrs.status` (HS.VS),
with the same dual access/dirty rules as FP. Vector memory captures MEM guest
metadata alongside its local decision; WB slow faults use their request-owned
metadata. The macro retirement payload arbitrates and registers that provenance
with its outcome before updating the retained scalar WB instruction. Do not
sample it later from a live MMU sideband. Vector requests always use Normal
translation intent, never a concurrent scalar HLV/HLVX/HSV selector.
Existing activity/drain interlocks hold context through deferred effects, with
an assertion at CSR redirect. Keep scalar overlap rather than serializing every
vector instruction. Guest regressions cover status gates, FP vectors, two-page
authorization, precise indexed restart after HFENCE, fault-first truncation,
and interrupt draining.

Explicit guest loads/stores carry `RiscvGuestMemoryAccess` from ID through WB.
Only the hypervisor specialization wraps virtual pipeline and transaction
requests with `RV5StageGuestMemoryReq`; the MMU consumes that intent before
physical admission. Keep HU/V legality in ID and capture it with the instruction;
SPVP chooses translation privilege without changing live execution state. HLVX
retains the load fault class and unsigned result, with a separate execute-read
permission selector. Do not turn it into an instruction fetch or apply its X
check to implicit PTE reads. Stores retain ordinary WB authorization.

Select `rv5stage-hypervisor-core` for real core/frontend/MMU execution against
delayed physical memory, precise guest faults, delegated VS traps, HS returns,
MPRV/MPV accesses, ordered HFENCE remapping, and explicit guest memory widths,
HU/SPVP legality, warm permissions, and precise denied-store behavior. Pair it with the host core and
MMU replay regressions and `riscv-hypervisor-csr` when changing this boundary.

Run `tools/run-racket-tests.sh riscv/tests/hypervisor-test.rhm riscv/tests/csr-test.rhm cores/csr/tests/csr-test.rhm`
and `FIXTURES='riscv-hypervisor-csr riscv-csr' python3 tools/testing/rsim/run.py`
when changing this boundary. The [SoC mandatory-requirement gate](../../socs/tests/udb-test.rhm)
checks the published RVA23 declarations.

For shared replacement-policy changes, run `cache-replacement`,
`cache-icache`, `rv5stage-dcache`, and `rv5stage-dcache-rv32`. The standalone
fixture covers four-way tree ordering, invalid-way priority, and a padded
three-way tree; the cache fixtures cover access and installation updates in
their real pipelines.

For the EX/MEM/WB load path, run `rv5stage-load-hit`, `rv5stage-mmu-replay`,
`rv5stage-dcache`, `rv5stage-dcache-rv32`, and `rv5stage-core`. The integrated
load-hit fixture checks a cold fill, one-bubble dependent addresses, bubble-free
vvadd-style warm loads, signed/unsigned lanes, FP hits, exactly-once device
reads, store-to-load ordering, and a younger lookup squashed by an older WB
fault. Keep the timing regression at the real core/MMU/router/L1D
boundary rather than replacing the cache with a fixed-latency response stub.
IO-MSHR request readiness is registered state,
not a dependency on its request payload or whole `drained` output bundle.
Include `rv5stage-fp-pipeline`, `rv5stage-core-rv32f`, and `rv5stage-core-rv64d`
for FP-hit writeback or shared payload changes.

### Ziccrse progress gate

The full-core test matrix for the [Ziccrse integration
guarantee](README.md#lrsc-eventuality-ziccrse) is:

```sh
FIXTURES='rv5stage-lrsc-core-progress rv5stage-lrsc-core-progress-predicted rv5stage-lrsc-core-progress-rv32' python3 tools/testing/rsim/run.py
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
The [SoC progress tests](../../sims/DEVELOPING.md#lrsc-system-validation)
adds real MiniRV5StageSoC, SingleCoreRV5StageSoC, and eight-hart TiledSoC/RV5Stage memory paths through normal FESVR.
The expanded validation passed on 2026-09-08 against `d78ce435` production
RTL: 108/108 core cases, all three FENCE.I checks, 12/12 SingleCoreRV5StageSoC placements,
12/12 eight-hart TiledSoC/RV5Stage placements, and 81/81 concrete memory-map checks.
MiniRV5StageSoC subsequently passed 12/12 placements on the same date through its
ordinary harness and forwarding HN-F/internal CHI RAM. Its compressed-disabled
profile uses word-aligned page-boundary starts and page tables inside its
64-KiB RAM; the six placements still cover LR.W/SC.W and LR.D/SC.D in both
Bare and Sv39. The adapted shared payload also passed all 24 SingleCoreRV5StageSoC/TiledSoC-RV5Stage
placements on their previously validated simulators.
Only test fixtures, payloads, build targets, and documentation
changed; no further production RTL fix was required for this matrix.
MiniRV5StageSoC, SingleCoreRV5StageSoC, and the TiledSoC RV5Stage configuration now enable the explicit `ziccrse` profile claim;
generic profiles remain opt-out. Preserve the matrix as a regression
gate when changing fetch, translation, reservations, or coherence arbitration.
Do not enable another integration from cache-only results or treat this bounded
regression as a proof for every translation and fabric-fairness scenario. In particular,
extending the reservation timer alone does not establish progress across
translation arbitration and replay.

For Ziccif/Ziccamoa validation, select `rv5stage-fetch`, `cache-icache`,
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
The reusable `RiscvHartCHIConfig` still allows restricted cacheable maps. The top-level
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
Account for the [UDB 0.1.17 applicability limitation](README.md#cache-block-and-reservation-bounds)
when validating CMO-free configurations. Do not omit the hardware fact or
silently enable CMO decode to satisfy that database version.
Select `cache-icache`, `rv5stage-dcache`, and `rv5stage-dcache-rv32` for
direct rsim validation. The data-cache benches cover all 64 CBO byte offsets,
aligned word/doubleword LR/SC sites on both sides of a 64-byte boundary,
neighboring-line isolation, exact SC matching, and one-shot reservation use;
the RV64 bench also covers invalidating snoops. These size/boundary regressions
do not establish eventual LR/SC success under adversarial coherence traffic.

For Zkt, run the architecture/profile/advertisement checks and the RV32 and
RV64 integer timing fixtures:

```sh
tools/run-racket-tests.sh riscv/tests/zkt-test.rhm cores/rv5stage/tests/rv5stage-zkt-test.rhm cores/rv5stage/tests/profile-test.rhm cores/rv5stage/tests/udb-test.rhm socs/tests/udb-test.rhm
FIXTURES='rv5stage-zkt-rv32 rv5stage-zkt-rv64' python3 tools/testing/rsim/run.py
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

For Zvkt, run the pure scope, probe-coverage, profile/UDB, and differential
vector timing checks:

```sh
tools/run-racket-tests.sh riscv/tests/zvkt-test.rhm riscv/tests/gnu-toolchain-test.rhm cores/rv5stage/tests/rv5stage-zvkt-test.rhm cores/rv5stage/tests/profile-test.rhm cores/rv5stage/tests/udb-test.rhm socs/tests/udb-test.rhm
FIXTURES='rv5stage-zvkt' python3 tools/testing/rsim/run.py
bash socs/tests/run-device-tree.sh
```

The probe generator intersects the exact architecture-owned Zvkt list with the
implemented V and Zvbb catalogs and encodes both unmasked and available masked
forms through their descriptors. Twin sequencers receive identical instruction,
execution-mask, vector-control, retry, and stall inputs but distinct active,
inactive, tail, old-destination, carry-mask, and merge-mask data. Compare
admission, issue, result-control, and authorization timing; do not compare
architectural result data. The explicit
gather indices and variable slide distances are control operands and must remain
equal between lanes. Retain the vector mul/div fixture and scalar Zkt multiplier
contention regression as the fixed-latency evidence for the shared iterative
multiplier. This is differential RTL validation plus a source-level timing
argument, not exhaustive formal noninterference or physical side-channel
certification.

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
FIXTURES='rv5stage-zawrs rv5stage-wfi rv5stage-dcache rv5stage-memory-router rv5stage-mmu-replay' python3 tools/testing/rsim/run.py
```

The WRS bench checks retirement deltas through CSRs, original trap PC/value,
globally masked and enabled interrupt wake, timeout and privilege policy, and
invalidation before and during entry. `rv5stage-hypervisor-core` additionally
checks VS/VU `hstatus.VTW` timeouts, `mstatus.TW` priority, short waits, and
reservation-loss/interrupt wakeups through the retained WB owner. Guest NTO
timeouts raise virtual-instruction only when TW does not require illegal-instruction;
ordinary wakeups win over either timeout, and STO never traps for TW/VTW.
Cache and adapter benches cover the
reservation level independently of instruction waiting. Keep pending WRS
context in the core, LR/SC state in L1D, and timer/interrupt policy independent
of physical clock gating. Select Zawrs through `RV5StageExtensions`, keeping
core decoder selection, ISA descriptions, and UDB claims derived from that
same profile. The UDB database names the ratified extension version `1.0.0`.

For the optional Sscofpmf integration, select `riscv-sscofpmf-rv32`,
`riscv-sscofpmf-rv64`, and `riscv-sscofpmf-rv64h`. These drive the real
CSR commit and interrupt-boundary interfaces; the reusable `riscv-hpm-*`
fixtures own exhaustive mode-filter combinations. Keep counter events attached
to the existing precise WB retirement signal. Pending interrupt state belongs
to the CSR file, independently of the reusable counter's sticky OF bit.
`rv5stage-sscofpmf-core` executes a retirement-overflow program through the
whole pipeline and checks that an older accepted store drains before entry.
Run the profile/UDB host checks when changing advertisement or counter masks.

For Zihpm CSR catalogs, profile claims, and access semantics, run:

```sh
tools/run-racket-tests.sh riscv/tests/csr-test.rhm riscv/rtl/tests/riscv-csr-bank-test.rhm cores/rv5stage/tests/profile-test.rhm cores/rv5stage/tests/udb-test.rhm
FIXTURES='riscv-zihpm-rv32 riscv-zihpm-rv64 riscv-csr' python3 tools/testing/rsim/run.py
```

The two Zihpm benches share an XLEN-parameterized sweep of every HPM slot,
write-ignore behavior, read-only write intent, RV32 high halves, and S/U
access denial. Keep the CSR-bank grouping in the RISC-V adapter and profile
permission policy in this core; do not add counter state for zero-valued slots.

For data IO-MSHR admission, ordering, and shared RN-I contention, run:

```sh
FIXTURES='rv5stage-memory-router rv5stage-uncached rv5stage-io-mshr rv5stage-io-boot' python3 tools/testing/rsim/run.py
```

The router fixture covers RV32 permission rejection and cached/uncached
exclusion. The composed IO-MSHR fixture covers RV64 retained payloads, fetch
arbitration and cancellation, backpressure, exactly-once completion, and reset.
The complete-core boot fixture executes the generated polling ROM with delayed
entry publication, secondary-hart parking, and fence-ordered signature stores
at three CHI response latencies. It requires one L1I line read for the polling
ROM and one for the payload, allowing additional distinct speculative lines,
with no D-cache traffic or ROM CompAck. Its C++ driver checks the public CHI
transactions and boot-ROM output words directly. Keep simulator entry programming and SoC
BootROM policy separate from this core-level regression.

For instruction-router flow changes, select `rv5stage-instruction-memory-router`
and `rv5stage-io-boot`. The router bench covers registered cached outcomes, S1 kill, explicit replay,
exactly-once uncached acceptance, stalled IO, and flush cancellation.

For Zicbom, use the composed decode test and the WB, MMU, physical-router,
and self-snooped cache fixtures:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/zicbom-test.rhm cores/rv5stage/tests/rv5stage-test.rhm cores/rv5stage/tests/udb-test.rhm
FIXTURES='riscv-csr' python3 tools/testing/rsim/run.py
FIXTURES='rv5stage-zicbom rv5stage-mmu-replay rv5stage-memory-router rv5stage-dcache rv5stage-dcache-rv32' python3 tools/testing/rsim/run.py
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
FIXTURES='rv5stage-core rv5stage-data-fault rv5stage-zicboz rv5stage-interrupt rv5stage-wfi' python3 tools/testing/rsim/run.py
FIXTURES='rv5stage-core-rv32f rv5stage-core-rv64d' python3 tools/testing/rsim/run.py
```

The RV32F/RV64D benches exercise rejected memory dispatch, committed prefetches,
deferred FP and atomic completion, FP stores/loads, CSR flags, and suppression of younger
FP/register/memory effects behind a data fault. RV64D uses longer fixed delays
than RV32F to exercise deferred versus WB-time returns. The killed-arithmetic
case samples the scalar adapter's public launch/result ports to prove arithmetic
actually ran, while trap-handler stores check that its FPR write and flags did
not escape. Keep architectural checks at the core boundary and avoid generated
temporary signal names.

For Zicboz, keep permission/fault ownership above the cache, and exercise
both XLEN SRAM sequences as well as the one-completion uncached sequence:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/zicboz-test.rhm
FIXTURES='riscv-csr' python3 tools/testing/rsim/run.py
FIXTURES='rv5stage-zicboz rv5stage-memory-router rv5stage-mmu-replay rv5stage-dcache rv5stage-dcache-rv32 rv5stage-uncached' python3 tools/testing/rsim/run.py
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
FIXTURES='rv5stage-data-fault' python3 tools/testing/rsim/run.py
FIXTURES='rv5stage-wfi' python3 tools/testing/rsim/run.py
```

The [rsim test guide](../../tools/testing/rsim/README.md) owns behavior-fixture
selection and artifacts; the [CIRCT contributor guide](../../tools/testing/circt/DEVELOPING.md)
owns the retained HDL integration checks. SoC integration belongs to
[`../../socs/DEVELOPING.md`](../../socs/DEVELOPING.md), and executable target
coverage belongs to [`../../sims/DEVELOPING.md`](../../sims/DEVELOPING.md).
