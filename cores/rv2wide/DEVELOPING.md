<!-- Defines RV2Wide stage, memory-ownership, decode-composition, and validation rules. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RV2Wide

Read [README.md](README.md) for the current public contract. This guide owns
the fetch frontend, integer slice, age/order rules, and behavioral fixtures. Follow
[core ownership](../DEVELOPING.md), the repository
[package graph](../../rhodium/DEVELOPING.md), and
[test policy](../../tools/testing/DEVELOPING.md).

## Architecture and ownership

Keep stage logic in one pipeline-ordered `core.rhdl`. Flow owns feed-forward
storage; the issue window owns prefix admission, retention, and coalescing.
No independent lane handshake may allow a younger instruction to pass an
older blocked instruction. WB is the only transaction and retirement authority;
its accepted deferred owners later write through the shared younger-slot port.
Redirect qualification gates new transfers as well as flushing pipe state.

Reuse ALU, branch, load/store shaping, and shared scoreboard components and the `cores/*-decode.rhdl` instruction
relations. Never import RV5Stage or another named core. Do not create an
instruction-kind enum followed by a second runtime control decoder.
Core control rows join exact canonical instruction patterns using
`component_output`, following RV5Stage's composition pattern. Unobserved
payload controls stay don't-cares behind cared enables/source-use bits.
The complete B catalog joins `RV64BAluCases` into each existing decoder.
Operand policy distinguishes binary, unary, and immediate B operations; only
observed routing fields are constrained. Reuse each slot's existing shared ALU
and forwarding paths rather than adding a bit-manipulation unit or decoder.
Zicond joins the shared `ZicondAluCases` with ordinary binary operand controls.
Zimop uses the existing addition controls with both operands selected as zero;
no source-use bits are set. Its SYSTEM opcode does not imply CSR action or
serialization. Both extensions retain the normal writeback/destination policy.

Event annotations live on functional Flow boundaries, independently of the cosim
source contract. The issue window exposes Valid candidates; each lane's named
window contract observes the actual compacting count and prefix consumption,
replicating a packet owner for its one/two appended instructions. The assembler
retains a trailing block owner and joins it with the live block for straddles.
EX/MEM mapping and lane grants certify manually authored combinational regions.
Split ownership uses its actual capture/release/pending controls. Successful WB
parents accepted load/divide requests; EX parents the multiply's existing owner
path, whose WB authorization filter discards killed products. Do not add a whole
module trace contract across already contracted Flow children. The shared divider
owns its intrinsic retained request-to-response contract.

Retirement prediction captures derive from the WB owner's retained resolved
successor and effective prediction. Keep next-PC comparison separate from the
RAS-action comparison; the recovery flag intentionally combines both and is not
an accuracy metric. Do not infer retirement or prediction results from matching
PCs in the host. The instrumented `rv2wide-fetch` oracle compares both slots'
PC, raw instruction, prediction result, and RAS mismatch after settled callbacks,
using its independent instruction execution and public prediction payloads.

## Implementation map

| Owner | Responsibility |
|---|---|
| `decode/operand-ctrl.rhdl` | Source-use bits, ALU operands, canonical immediate selection |
| `decode/mem-ctrl.rhdl` | Load/store enable, direction, width, signedness, inactive care masks |
| `decode/system-ctrl.rhdl` | Architectural CSR operation/source, trap/return/wait, and serialized fence columns |
| `decode/long-ctrl.rhdl` | Shared multiply/divide relations, service selection, inactive care masks |
| `decode/core-ctrl.rhdl` | Selected instruction domain, writeback column, one combined relation |
| `bundles.rhdl` | Instruction, lookup/admission/response, stage, and retirement contracts |
| `issue-window.rhdl` | Sole eight-entry compacting instruction buffer, free-entry count, prefix consumption |
| `frontend.rhdl` | Credited block fetch, S1 translation/permissions, local replay, and block fault ownership |
| `instruction-assembler.rhdl` | Flow block storage, mixed-width parcel consumption, shared C expansion, and continuation faults |
| `core.rhdl` | RR/EX/MEM/WB, forwarding, shared component/CSR instances, register state, precise traps |
| `load-response.rhdl` | Four accepted contexts, atomic response/owner joining, shared load extraction |
| `long-execution.rhdl` | EX multiply reservations and WB owner validation, retained divider ownership |
| `cache.rhdl` | Physical permissions, shared L1D adaptation, and ordered IOMSHR/uncached routing |
| `mmu.rhdl` | EX indexing, MEM translation, separate TLBs/shared walker, WB miss priority, physical-response ownership |
| `fp.rhdl` | RR fixed-return bookings, EX arithmetic launch, WB authorization, FPR hazards and load/arithmetic write ports |
| `rv2wide.rhdl` | Frontend/core/shared L1I/L1D composition, distinct CHI identities, start/halt boundary |
| `profile.rhm`, `udb.rhm` | Lean RV64IMACB/RV64IMAFDCB architectural description, shared RTL/metadata CSR specialization, and implementation-owned UDB choices |
| `hart.rhdl` | Core-neutral SoC port adaptation and one reset-vector start per reset epoch |
| `tests/circt/` | Production-core emitter and independent sequential-result/ordering oracle |

## Change workflow

1. Update the public contract before broadening the implemented execution slice.
2. Add canonical instruction descriptors to the chosen domain and complete
   every relevant control column; preserve care masks for inactive controls.
3. Keep stage result ownership and oldest-stop priority explicit. An unavailable
   youngest match must not fall back to an older producer. Never replay an
   already accepted memory transaction.
4. Extend the existing behavioral fixture at the relevant boundary. Keep
   generated MLIR, SystemVerilog, and simulator binaries out of version control.
5. Add complete profile/SoC integration only once its declared features work;
   a decoder domain is not an architectural profile claim.

MEM resolution is currently same-cycle qualification for the token exposed on
`memory_stage`. It is not permission to return an untagged response later.
Actual memory uses EX lookup, fixed MEM outcome, and WB store/slow authorization.
Neither EX nor MEM waits for readiness. Missing or rejected service replays at
WB; accepted transactions allocate an owner and cannot replay. Admission must
certify synchronous fault freedom, as for RV5Stage's aligned ordinary service.
Do not introduce late fault responses without retaining retirement ownership.

Ordinary misaligned operations use the separate shared `RiscvSplitAccess`
contract. Capture their WB owner before draining older deferred work, squash
younger pipeline/window state, then launch once. Retain that owner through the
single final Valid outcome; successful completion retires in slot zero and
refetches its successor, while a fault enters normal retained-trap handling.
Do not allocate a load-response/scoreboard owner or reuse the admission-certified
aligned response port. Interrupt entry waits for this noncancelable owner.

`cores/misaligned-access.rhdl` owns fragment masks, store positioning, and
load assembly; this MMU owns each fragment's translation/PMA check. Fragment
responses, ordinary core replies, and PTE responses share one ordered physical
owner FIFO and are routed with `zip_flow`/`demux_flow`. The second fragment must
wait for first-fragment completion, and fault provenance must use the engine's
virtual fault address, not its aligned physical request. Preserve accepted
prefixes on later faults. Check the full eight-byte footprint against normal,
cacheable, idempotent memory; do not widen device accesses into this path.

Preserve natural access width through MMU translation. Cache service aligns its
own beats; uncached service keeps the exact address/size and unpositions store
data for the shared StoreGen. Its opaque three-bit context restores a returned
scalar to its raw-beat lane for the core's existing LoadGen. Do not issue
speculative physical requests to devices or use widened PMA checks that reject
legitimate sub-beat regions. Page-table traffic still requires cacheable RAM.

The IOMSHR admits only after L1D drains; its registered busy state gates cached
admission and speculative physical resolution. Carry that state back through
the MMU as `ordered_busy`: WB must replay even an earlier MEM hit while IO is
outstanding. Gating only slow requests misses that race. Neither the core nor
speculative flush cancels an accepted IO owner. The physical response paths are
mutually ordered and merge using Flow arbitration, preserving the MMU owner FIFO.

Keep accepted ownership independent of speculative flush. The memory owner FIFO
joins ordered responses through `zip_flow`, normalizes returned data with
`LoadGen` for ordinary loads, passes architectural LR/AMO values and SC status
unchanged, and backpressures at the common completion arbiter. Round-robin Flow
arbitration merges variable load and divide results and reserves the younger RR
slot for responses requiring a GPR write.
An unflushable three-stage Valid pipe aligns those responses with the vacant WB
slot. Mux the completion and younger instruction before the second register-file
write connection; do not add a third write port or backpressure WB. The
completion-pipeline occupancy participates in precise fault drain.
Pre-WB producer checks bridge the interval before the scoreboard is set.
RAW interlocks select the youngest older producer; WAW interlocks cover all
older deferred producers. A completion forwards at arbitration and from each
return-pipeline stage through its RF write edge. Keep source readiness separate
from destination readiness: a returned value permits RAW consumers immediately,
but its scoreboard reservation blocks WAW until the actual RF update.

Same-group WAW only splits when the older writer may complete through a deferred
service: memory, M, and late FP-to-GPR writers remain interlocked, including speculative load hits.
Normal-WB older writes may pair with normal-WB or deferred younger
writes. Keep both architectural retirement records and per-instruction values;
coalesce RF writes only when both normal-WB write enables are qualified and the
destinations match. A faulting, replaying, split, or deferred younger instruction
must not suppress the older write. The second port can carry an older completion,
so never infer younger-write priority from the port number. Insert direct FP
returns into their WB lane's forwarding payload; late FP returns precede the
pipeline's age-ordered producers. An older FP bypass must not override a younger writer.

RR may waive the younger rs1 RAW/read interlock only for a zero-immediate ordinary
integer load whose base is the same-pair older non-memory, non-M, non-FP, non-system, non-branch
writer. The older writer still passes every ordinary admission interlock.
Carry `address_from_older` across the existing EX register; select slot zero's
ALU result directly as the younger address, after rather than before the younger
ALU. Never route this bypass through another dependent addition. Keep normal
MEM checks, WB authorization, split-access ownership, and fault/replay priority.
Every other source and destination hazard retains its ordinary interlock.

M instructions use the shared physical control relations in the same composed
decoder. Only one memory-or-M deferred destination may issue per group, matching
the one scoreboard set port. Long operations disable ALU forwarding until their
completion is available for forwarding. x0 M results need no service owner.

EX launches the five-stage multiplier. An unflushable two-stage owner path reaches
WB at the same time as its instruction; WB retirement authorizes a three-stage
continuation to the product. RR books EX+5 (RR+6) in the shared fixed GPR calendar.
The authorized product writes and forwards directly on that edge. A killed,
replayed, or faulted owner drains without publishing the speculative product;
its unused booking simply ages out. Keep the multiplier feed-forward, with no
result queue or extra return stages. The outstanding-owner count participates
in precise drain, not capacity admission. FP bookings have first-client priority;
same-group multiply/FP claims for the same edge split at RR before either
executes. Other fixed pairs can book independently. Calendar bit three blocks
younger-slot issue and variable-return admission for their future WB edge.
Pause new fixed bookings after bounded variable-return starvation. No third
write port or WB backpressure is permitted.

Division captures its operands and owner only after WB acceptance. A busy service
causes preacceptance replay, while an accepted operation survives all speculative
flushes. Word operations normalize both inputs before division and sign-extend the
selected low word afterward. Precise drain includes multiplier reservations,
divider ownership, load owners, current accepted requests, and completion writes.

A MEM prediction correction clears the instruction buffer, RR, and EX, including EX
lookup admission, while preserving its own MEM-to-WB transfer and older WB work.
An older mispredicted instruction suppresses its same-group younger MEM transfer;
a correctly predicted taken branch preserves its target-stream peer.
Branch selection observes both lanes' resolved MEM outcomes. WB faults/replays
and retained faults override MEM recovery; branches never redirect twice.
An older same-group memory instruction may reject WB authorization after MEM
branch recovery; its WB redirect must override that speculative target and
suppress the younger branch's retirement and predictor training.

### Predictor ownership and recovery

The frontend instantiates shared `Btb` with `fetch_bytes = 8` and shared `Ras`.
S0 admits a PC into the registered S1 lookup. S1's combinational BTB/RAS result
selects the next S0 request directly and travels with its original lookup into S2.
Byte-six continuations retain their prefix prediction instead of looking up a
new branch. Replay retains that continuation context; the assembler also retains
the prefix prediction across straddles. A saved cursor holds an unaccepted
successor when array admission or block credits prevent a request.
Instruction metadata survives issue-window compaction and every pipeline stage.
Do not infer prediction ownership from a lane or an aligned block PC.

Assembly validates predictions while expanding the existing two candidate
instructions. J/B immediate targets override matching taken BTB targets; target
mismatches invalidate the stale entry. Only JAL/C.J and RAS returns discover
fallbacks; never discover a conditional branch through the BTB's unconditional
discovery port or infer an unknown direction from its immediate.
Architectural restart/redirect, accepted assembly repair, and S2 replay take
priority over S1 prediction and the saved cursor in the S0 request mux.
These replacements reuse canceled reservations rather than waiting a cycle for
registered credits to clear. On accepted assembly repair, clear old S1/S2
validity at that edge, but retain a replacement S0 request accepted into S1.
Kill the younger cache S1 resolution, but do not suppress the current cache S2
response or gate the assembler's block/input offer with its own repair. This
keeps the live-queue bypass acyclic and preserves the accepted instruction prefix.
The assembler's registered `repairing` clears queued suffixes on the following
edge, before the replacement request reaches S2. Local repair/replay kill lookup
contexts but do not flush cache-local accepted refill/error ownership. Retained
refill errors are line-address-qualified; a canceled token cannot publish them.
Only architectural commands flush the cache, which permits a replacement S0
array lookup at the same edge. Architectural commands retain priority.
The same path handles stale boundaries and direct-jump/RAS fallbacks.
Speculative RAS actions occur only on instruction packet acceptance, with at
most one action per packet; fetch replay and local repair preserve older actions.

EX carries actual successor, branch update, and misprediction through MEM/WB.
MEM correction preserves its own transfer and kills younger tokens. WB alone
qualifies training and resolved RAS actions from the successful retirement
prefix. MEM flush restores through current WB, and the surviving corrected
branch restores again at WB with its actual action included. Pause assembly
speculation on this reconciliation edge because RAS restore wins speculation.
WB faults/replays restore without training rejected branches. Architectural
translation invalidation and FENCE.I clear both predictors, not merely fetch state.

A pending fault squashes younger pipeline/window work immediately, but its
CSR trap command and public redirect wait for both the owner FIFO and memory service to drain.
Same-group older acceptance participates in that drain decision. Branch and
replay redirects do not cancel or wait for successful accepted work.
Reset is coordinated with the memory service; it ends the entire response epoch.

The frontend pairs one accepted S0 virtual index with S1 physical permission
resolution and an S2 block outcome. Both lookup contexts use flushable Flow
Valid pipes. Reserve one of the assembler's three block slots for each admitted
lookup using registered occupancy. The Flow ShiftQueue has live bypass; two C
expanders feed the existing eight-entry instruction window without another
instruction queue. The assembler retains a consumed-halfword cursor and at most
one unfinished 32-bit prefix. Advance this state only with packet acceptance or
when saving an incomplete prefix that produces no packet. Never depend on current
issue readiness to accept a cache result. Replay kills younger lookup stages only
and preserves buffered blocks and prefixes; MEM/WB recovery clears assembly and
the instruction buffer. Start is
quiescent-only and the buffer is already empty. Accepted refills belong to L1I
and remain live across speculative cancellation. Fetch faults carry explicit
cause/address through the ordinary instruction token and suppress decode
hazards, memory requests, branch effects, and GPR writes.
Keep canonical decode bits separate from raw encoding and sequential PC. Link and
system recovery use the sequential PC; illegal trap values use the raw encoding.
A faulting continuation uses the retained prefix's instruction PC and the next
block's fault address. The CSR bank's C configuration must agree with this IALIGN.
`RV2WideConfig.compressed` retains C as the mandatory base and accepts optional
Zcb/Zcmop additions, validated through the shared architectural catalog. Pass
both that list and the FP profile into the shared expander; C includes Zcd when
D is selected. CSR specialization and ISA/UDB metadata consume the same list.
Zcb expands to existing scalar controls, and C.MOP expands to an operand-free
ADDI x0,x0,0; neither needs another execution decoder or scheduling policy.
The core's immediate `fetch_flush` pulse also stops fetch during a retained WB
fault, before its drained public redirect. A simultaneous redirect supplies the
new cursor; otherwise fetch remains stopped. Buffer credits alone cannot express
this cancellation because a cleared buffer can still be rejecting admission.

The data adapter exposes the shared cache's `pipeline_lookup` (EX virtual index)
and `CachePipelineAccess` (MEM physical resolution, following WB store commit).
The MMU owns exactly one request-context register between these phases; do not
put another register in the adapter or make EX admission depend on a TLB result.
WB data demand wins DTLB contention over MEM, whose token receives Replay.
Only WB data misses enter the retained pending-walk slot. An instruction walk
already in progress finishes first, and the pending WB lookup wins next admission.
The walker captures its request context, independent of subsequent live CSRs.

Architectural invalidation clears TLBs and retained faults at the retirement edge.
Registered walker cancellation breaks the retirement/readiness feedback path;
the invalidation edge suppresses completion publication and new walk admission.
Core/PTE physical requests use fixed core-first arbitration and an atomic request/
owner fork. The owner FIFO is never flushed: it routes each accepted response
even when the walker is draining a canceled epoch. A PTE admission fault consumes
the walker offer without allocating a physical response owner. No whole-walk
cache lock may block a translated older WB access behind younger fetch traffic.

Keep `start` reset-only, with the data service drained. It is not an asynchronous
core flush input. The integrated top follows CSR trap/return redirects as well
as branches/replays; WFI has a separate sleep indication.

Atomic controls come from the pure A catalog and compose with the existing
operand, ALU-address, branch, writeback, system, and long-service columns.
Address generation adds zero to rs1; LR must not depend on its encoded zero rs2.
AMO controls are uncared for LR/SC and non-AMO operations. Preserve natural
W/D width and unshifted rs2 through translation and authorized cache service.

RR drains older memory pipeline tokens and accepted service work before an
atomic enters EX, without draining independent integer/M work. EX/MEM check
alignment, translation, and PMA but never physically execute an atomic.
WB alone allocates its owner and sets `wb_ordered_memory_pending`; all younger memory
retirement, including previously resolved hits and store candidates, replays
until the atomic response/owner join transfers to completion arbitration.
This stronger ordering implements every aq/rl combination without separate
acquire/release queues. The pending flag is not speculative flush state.
Reservation and RMW behavior stay entirely in the shared L1D.

The adapter sends ordinary loads/stores as raw aligned beats, but sends atomics
with their exact address and natural width. Their virtual index must preserve
the same page offset as the physical request, including W at byte lane four.
LR/AMO results are sign-normalized by L1D; SC status must not pass through
LoadGen. Both speculative and authorized PMA checks reject non-atomic RAM and
device/noncacheable atomics. PTE requests remain ordinary raw-beat loads.

Optional Zicboz joins the canonical CBO.ZERO pattern with rs1-plus-zero operand
controls, the shared cache operation, and disabled register-write controls.
The profile selects decode, CSR CBZE fields, ISA metadata, and UDB publication
together. It uses the same ordered-memory pending owner as atomics, but must
not inherit their alignment, read-permission, or atomic-PMA requirements.
MMU MEM qualification and physical admission check the full aligned 64-byte
block and its dedicated zero capability. Only ordinary loads/stores enter the
speculative physical pipeline; a CBO always uses the WB service. Keep the
original VA through retirement/fault handling and align only the footprint or
physical service. The existing shared L1D and uncached engine own zero execution.
The cache/core/MMU fixtures cover whole-block writes, permissions, fault VA,
non-speculation, and accepted-owner ordering; the native cosim adapter test
checks translation-preserving store fragments and rejected attempts.

Optional Zicbom composes the same address/no-destination columns with the three
canonical maintenance operations. Only one memory instruction issues per group;
the older management candidate owns the CSR permission probe and an unissued
younger candidate is probed again after compaction. Preserve the effective
invalidate-to-flush operation in the pipeline controls. Current execution
privilege controls CBCFE/CBIE; MPRV affects only translation.

Management shares the split access's retained WB owner, not the deferred load
FIFO. Capture squashes younger work while permitting an older peer to retire.
Latch completion of prior-work drain before presenting the request: live
`memory.drained` may depend on current admission and must not gate that same
offer. Retain translation retries and request backpressure without reexecuting
the instruction; after acceptance consume exactly one ordered response, retire,
and redirect to the successor. Admission faults follow retained trap handling.
Keep the external split protocol and split-only cosim fragment observation
unchanged. Management's architectural retirement/trap reports no byte mutation.

The MMU uses `Sv39Access.CacheManagement` and both MMU/physical adapter check
the complete aligned block with the architectural read-or-write PMA helper.
Never feed management into speculative load/store resolution or the IOMSHR.
Statically uncached management drains cache/IO work and returns through a local
Flow queue, participating in ordered-busy and drained accounting. Coherent
management delegates self-snooping and completion to the existing shared engine.
Extend the existing core, MMU, and cache fixtures for permissions, retained
retirement, read-only mappings, whole-block faults, dirty publication, clean
residency, invalidation, and uncached no-IO completion.

Optional Zicbop overlays its three exact hint cubes on the ORI relation, using
the architectural `PrefetchImmediate` descriptor and shared
`CachePrefetchOperation`. Ordinary ORI encodings retain their original controls.
Hints have no memory-enable or GPR-write control. Fork successful WB retirement
into the hint path, then use Flow's fixed-priority Valid arbiter; simultaneous
younger hints may be dropped without changing retirement.

The MMU's independent TLB probe ports use effective data privilege and the
shared nonfaulting prefetch permission rule. They never arbitrate for a walker
or change demand lookup timing. Both virtual and physical hint stages flush on
translation-state changes or invalidation; outgoing validity is also suppressed
on that edge. Reject overflowing bare addresses and qualify the complete
aligned block before touching a cache. The production top routes instruction
versus data intent using Flow. The L1D adapter drops hints during outstanding IO
or local uncached management; shared caches own demand priority and busy drops.

Extend the existing fixtures rather than adding prefetch-specific CI lanes:
`rv2wide-core` checks signed offsets, both slots, simultaneous hints, neighboring
ORI encodings, and killed hints; `rv2wide-mmu` checks nonfaulting translation,
PMA drops, context cancellation, and no walk allocation; `rv2wide-cache` and
`rv2wide-fetch` check real CHI prefetch traffic followed by demand hits.

## System operations and precise boundaries

RR serializes system instructions after older pipeline and accepted memory work
drain and never admits a system instruction as the younger slot. CSR results
are unavailable to speculative forwarding; WB writes the shared bank's result.
Younger ordinary instructions may issue, but every WB system command flushes
their effects and refetches from the architectural successor or trap/return PC.
This cut gives WB CSR recovery priority over a younger MEM branch without
making feed-forward stage registers elastic.

FENCE and FENCE.I share this drain/serialization path, with no CSR action or
writeback. Successful WB FENCE.I drives a separate `instruction_invalidate`
Pulse alongside the successor redirect. The frontend forwards it to the shared
L1I invalidate input; L1I owns suppressing installation by old retained refills.
Do not emulate instruction invalidation with speculative flush alone.

The WB retention register holds a faulting token or interrupt boundary independently
of the speculative pipes. An older accepted load paired with a younger fault
retires once, preserves its completion owner, and delays trap entry until the
owner FIFO, service, and unflushable completion pipe are all empty. Interrupts
inhibit new WB memory acceptance before retaining the oldest unretired PC.
If an interrupt disappears while draining, recover to that PC without trap entry.
An empty-pipeline boundary uses the last retired successor (including branch
targets), never an arbitrary fetch cursor.

Only system/fault commands enter `RiscvCsrFile.commit`; ordinary instructions
feed the explicit two-bit retirement count. RV2Wide may count an older successful
slot on the same edge as a younger synchronous trap. RV5Stage continues to supply
zero/one. Keep CSR write priority and pre-transition privilege filtering in the
shared counters; completion is not retirement. Architectural CSR descriptors and
trap selection are reused, not copied into named-core control logic.

## FP scheduling and ownership

`fp.rhdl` owns the FPR file and the shared execution service. The core selects
`RV2WideFp` or the stateless `RV2WideFpDisabled` circuit at elaboration time;
the enabled implementation has no disabled-mode branches. RR books its
fixed-result calendar one cycle before EX and captures the selected slot's
three operands in the EX payload. The booking owns the execution input even
when a flush suppresses its request, so variable-request readiness cannot feed
back through same-cycle recovery. WB authorizes the resolved oldest successful
prefix independently of CSR command-success feedback. Authorization travels
for `latency - 2` cycles and qualifies the corresponding fixed numerical return.

The optional `RV2WideConfig.half_precision` and `zfa` select shared `fp_instructions`,
`fp_control_cases`, and the execution service's matching datapaths. Keep the
decoder, compressed-expansion domain, ISA description, and UDB projection on
those same selections. These subsets add no separate decoder, numeric unit owned by this
core, or writeback policy; immediate indices must not become register hazards.
FLH/FSH use `MemoryWidth.Half` in the composed memory controls. Reuse shared
load boxing, store shaping, and split access rather than widening the request
or introducing a half-specific memory path. Zfhmin selects conversion/move
support without half arithmetic or half Zfa operations.

The core books late FP-to-GPR writes against the younger GPR slot. At RR,
calendar bit three suppresses younger issue and unpredictable-completion
admission for that future WB edge. Fixed results bypass directly, not through
the unpredictable completion pipeline. The FP adapter separately interlocks
all FPR destinations, including f0, and forwards both FPR writes to RR.
An older MEM divide prevents next-cycle fixed booking; variable FP work does
not coissue with memory or integer long-latency work. It may pair with ordinary
integer instructions without introducing younger side effects before acceptance.

`rv2wide-core-fp` exercises 3/5/2-cycle execution; `rv2wide-core-fp-late`
uses 3/5/5 cycles to collide integer-returning FP operations with multiply returns.
Both enable Zfa; the first selects Zfh and the late fixture selects Zfhmin.
They share a public-interface bench covering both age slots, FPR dependencies,
loads/stores, killed EX arithmetic, divide, flags, and illegal FS/rm. Zfa cases
cover both precisions, static/dynamic rounding, NaNs, modulo conversion,
dependent GPR returns, and suppression of killed destination/flag/FS updates.
Half cases cover boxing, conversions, every aligned halfword lane, delayed and
split memory completion, dynamic rounding, and FS/rm legality. The full subset
also exercises arithmetic, fused operations, comparisons, integer conversions,
half Zfa, and killed EX/WB work; the minimal subset checks its arithmetic boundary.
`tests/profile-test.rhm` checks conditional ISA/UDB publication and invalid
half/Zfa-without-FP selections. Native
adapter tests shuffle callbacks while closing out-of-order fixed FP, variable
FP, and concurrent load/arithmetic owners. Keep wider software coverage in the
existing ISA-smoke lanes, not a separate FP qualification matrix.

## Validation

For tracing changes run the batched `event-window` behavioral fixture and the
Mini/Simple trace smoke with the native Perfetto importer. Use `COSIM=1` on one
shape to check pass composition; ordinary core behavior is covered by the existing
`rv2wide-core` fixture. The trace smoke reuses the existing scalar cosim payload,
without adding software or CI config rows:

```sh
FIXTURE=event-window bash tools/testing/circt/run.sh --simulate-only
make -C sims trace-smoke SOC=mini-rv2wide-rv64imacb COSIM=1 TRACE_FILE=/tmp/rv2wide-mini.pftrace TRACE_PROCESSOR=/path/to/trace_processor_shell
make -C sims trace-smoke SOC=simple-rv2wide-rv64imacb TRACE_FILE=/tmp/rv2wide-simple.pftrace TRACE_PROCESSOR=/path/to/trace_processor_shell
```

`observation.rhdl` names the passive `rv2wide.v3` contract. The core declares
WB slots, split capture, CSR commands, and accepted service returns through
`cores/cosim-source.rhm`; `rv2wide.rhdl` binds the sibling MMU's physical
provenance. Ordinary elaboration adds no observation ports, state, or DPI.
The simulation-owned [adapter](../../sims/cosim/DEVELOPING.md) assigns age IDs,
retains deferred owners, and resolves both slots at a settled sample barrier.
Keep replay and speculative EX multiply launch out of architectural admission.
The completion adapter follows the three unflushable RR-to-WB stages for load
and divide, but matches multiply directly to its authorized WB+3 RF-write edge.
It permits concurrent multiply writeback and variable-return admission. Update
that contract and its ownership tests if these latencies change.

For observation changes, run the native adapter and optional-pass tests, then
the existing software path on both shapes:

```sh
make -C sims cosim-hooks-test
tools/run-racket-tests.sh sims/cosim/tests/pass/pass-test.rhm
make -C sims boot-test isa-smoke cosim-smoke SOC=mini-rv2wide-rv64imacb COSIM=1
make -C sims boot-test isa-smoke cosim-smoke SOC=simple-rv2wide-rv64imacb COSIM=1
```

Run the focused production-core fixture:

```sh
FIXTURES='rv2wide-core rv2wide-core-fp rv2wide-core-fp-late' bash tools/testing/circt/run.sh --simulate-only
FIXTURE=rv2wide-cache bash tools/testing/circt/run.sh --simulate-only
FIXTURE=rv2wide-fetch bash tools/testing/circt/run.sh --simulate-only
FIXTURE=rv2wide-fetch-disabled bash tools/testing/circt/run.sh --simulate-only
FIXTURE=rv2wide-assembly-prediction bash tools/testing/circt/run.sh --simulate-only
FIXTURE=rv2wide-frontend-prediction bash tools/testing/circt/run.sh --simulate-only
FIXTURE=rv2wide-mmu bash tools/testing/circt/run.sh --simulate-only
make check-boundaries
```

The fixture belongs to `cores-execution-datapath`. It checks sustained dual
retirement, packet coalescing, age-ordered same-destination pairing, RAW/WAW and x0, youngest-producer forwarding,
RV64/word ALU operations, seeded dependency-heavy arithmetic, signed/unsigned
branches and jumps in either slot, JALR masking, misalignment/illegal faults,
MEM qualification, replay/restart, older-fault priority, and reset cancellation.
M scenarios exercise every RV64M encoding and result projection, signed/mixed/unsigned
high products, zero-divisor and overflow rules, overlapping pipelined multiplies
and loads, cross-service completion ownership, RAW/WAW interlocks, x0, and rejected
versus accepted operations across branch/trap recovery. A dependent multiply
consumer must reach MEM within six cycles of its producer's MEM token; repeated
consumers cover forwarding and same-register WAW release at the direct write
edge. Every accepted multiply completion must arrive exactly three cycles
after WB authorization (five after EX), including competing variable returns.
Sixteen independent multiplies must write on consecutive cycles, and both FP
timing variants exercise fixed-return pairing in each age order.
B scenarios cover every RV64 Zba/Zbb/Zbs instruction in both age slots, with
zero/all-one/sparse/mixed operands, shift boundaries, dirty upper words, and
independent paired work plus dependent consumers. A pending divider tests false
rs2 interlocks on unary/immediate encodings; mixed B/M sequences test deferred
forwarding and WAW. Zicond cases exercise full-width zero/nonzero conditions in
both slots, x0, RAW/WAW, and forwarding. Zimop covers every MOP.R/MOP.RR index,
dual issue, ignored sources with pending load/divide owners, destination WAW,
dependent consumers, wrong-path suppression, and illegal neighboring encodings.
The fetching fixture also passes B results through the real
LSU, compressed consumers, and a draining WFI. The oracle uses independent bit
loops and architectural result rules, not RTL decode/control fields.
It also checks all six CSR forms, source-index write intent, counter counts and
write priority, M/S trap state and returns, younger-fault older retirement,
WB-over-MEM CSR recovery, accepted-load drain before CSR/interrupt entry, live
and idle interrupt boundaries, and WFI local wake versus interrupt trap.
It checks branch recovery against the public MEM token before WB retirement,
simultaneous older WB recovery, same-group authorization failure after a MEM
branch, and filling the instruction buffer under issue backpressure.
The same fixture drives the production LSU boundary with a controlled cache
service. It checks hit throughput, every natural byte lane and load extension,
store masks, four outstanding owners, hit-under-miss, RAW/WAW scoreboarding,
completion/older-slot simultaneous writes and younger-slot reservation, capacity and admission replay, store-commit
rejection, lookup/admission faults, and accepted-work drain across precise stops.
`rv2wide-cache` instead connects the production shared L1D and a CHI backing-memory
oracle. It checks refill delay, masked stores, signed/unsigned loads, dirty
eviction, independent hits and ALUs during misses, redirect survival, and
unmapped-access faults after older accepted work drains.
Its atomic cases cover every W/D AMO, all aq/rl settings, both word lanes,
SC success/failure, address/width mismatch, store and CHI snoop invalidation,
x0 effects, and pairing with integer work. The core fixture checks LR versus
SC/AMO fault classes in either slot and branch-killed atomic authorization.
An accepted older LR paired with a younger fault must complete its delayed GPR
write before trap delivery.
The MMU fixture checks readable-only PTEs, atomic operation retention through
translation, and device PMA rejection; the fetching fixture executes atomic
dependencies, x0 mutations, and branch-killed SC through the production top.
It compares architectural retirement against an independent sequential model,
not internal register names. External qualification is matched to public MEM
PCs; no test-only RTL switches or hierarchical state mutations are used.

Misaligned cases span every supported H/W/D offset, signed/unsigned/x0 loads,
neighbor-preserving masks, word/line crossings, and older completion drain.
The MMU fixture stalls the prefix response, checks exact fragment masks/data,
and faults the second page without reissuing the prefix. The fetching fixture
uses nonadjacent physical pages, reads fault VA/EPC/cause from the trap handler,
and verifies a completed store prefix remains visible after a second-page fault.
The core fixture checks retained completion in either WB position and preserves
natural-alignment faults for LR/SC/AMO.

`rv2wide-fetch` executes the production fetching top against a byte-addressed
CHI backing-memory oracle. It checks sustained warm dual retirement, upper-only
restart packets, dependency/backpressure retention, cold I/D misses, masked
store/load execution, reordered instruction refill packets, wrong-path refill
errors across redirects, last-word-of-page retirement, precise unmapped/read-error
faults, misaligned starts, and reset/restart after fault. It also covers a younger
illegal instruction while an older load drains, with fetch cancellation preceding
the fault report. Fault scenarios end with a coordinated reset, while a handler
scenario programs mtvec, executes ECALL, reads mepc/mcause, returns with MRET,
and reaches WFI through real instruction-cache fetching. The existing cache-only
fixture composes the same production core and L1D adapter with packet stimulus;
it does not add a second public cached-core wrapper.
Mixed-width scenarios cover four compressed instructions per block, warm dual
retirement, delayed compressed loads/stores, C.JALR links and halfword returns,
illegal compressed mtval, cold line crossings, and Sv39 continuation-page replay,
success, and faults. Cross-page access/page faults check both mepc and mtval through
the real handler. Packet-level fixtures supply raw encoding and sequential PC
explicitly rather than relying on defaults in the core.
The predictor-enabled and disabled fetch instances reuse that entire oracle.
The enabled fixture additionally selects Zcb/Zcmop and executes every form,
all compact integer registers, dependent consumers, byte/halfword memory lanes,
and wrong-path store suppression. Both variants check original 16-bit trap
values after older load drain: reserved neighbors with the subsets selected,
and unselected optional encodings with C only. Profile tests check independent
selection, shared CSR configuration, publication, and invalid combinations.
The enabled instance additionally requires warm conditional BTB predictions,
including a 32-bit branch crossing an eight-byte fetch boundary. It also runs
compiler event instrumentation so accepted-prefix/local-repair ordering and
instruction ancestry are checked by RTL assertions. The
core fixture supplies explicit predictions to check correct taken branch/target
pairing and wrong-direction/target recovery; shared eight-byte cursor ordering
is covered by `bpred-btb-wide`.
`rv2wide-assembly-prediction` isolates stale entry boundary/length repairs,
immediate-target corrections without changing conditional direction, fallthrough
suffix cuts, predicted straddles and continuation faults, and
single-shot RAS actions under packet backpressure. Its D-enabled configuration
also checks all four compressed FP load/store expansions and raw encodings.
Local repair clears block and prefix ownership on the following cycle; it must
not clear the lineage of the same edge's accepted packet.
`rv2wide-frontend-prediction` supplies fixed-cycle instruction-cache responses
through public ports. It checks same-cycle S1 prediction and S2 target offers
for both slots,
compressed/backward jumps and cold/stale straddles, stale jump/conditional targets,
preservation of correct S1 predictions, stalled packet acceptance and corrected
cursors, younger fault/replay cancellation, and architectural redirect priority.
Keep SRAM/refill and event-lineage integration in the existing
enabled/disabled production fetching fixtures.

The fetching fixture also boots through satp/MRET into Sv39 supervisor code,
loads/stores through a separately filled DTLB, executes SFENCE.VMA, and checks
load, store, and instruction page faults through architectural mcause/mepc/mtval reads.
It also drives an independent HN-I responder for exact-width device and ordinary
uncached transactions, checks younger cache-hit ordering and wrong-path suppression,
and verifies PMA faults without bus effects. A completed device command publishes
new code into a resident instruction line; FENCE.I must observe it after draining
the delayed write acknowledgement. The fixture does not model late synchronous
CHI errors on the admission-certified data path.
Sv39 IO tests check PA routing and original-VA trap reporting; the MMU fixture
also rejects a device-backed page table before any physical request is admitted.
`rv2wide-mmu` controls the physical service boundary to check EX/MEM timing,
speculative miss nonallocation, current permissions, remapping after invalidation,
WB priority, canceled accepted-PTE draining, and exact physical-fault VA reporting.

The fixture runner invokes the repository-managed Racket wrapper. Run host
checks through `tools/run-racket-tests.sh` and other elaboration through
`tools/run-racket.sh`; do not bypass the managed compiled root. After changing
fixture ownership, confirm `--group cores-execution-datapath --list-fixtures`
includes `rv2wide-core`. Mini/Simple integration uses the existing shape harnesses
and their normal FESVR loading and BootROM path. Run `boot-test` and `isa-smoke`
with `SOC=mini-rv2wide-rv64imacb` and `SOC=simple-rv2wide-rv64imacb` for platform
changes. Keep CI enrollment in `sims/test-configs.txt` and workload selection
in the shared shape/ISA policy, not in the processor configuration.
