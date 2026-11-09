<!-- Defines Superscalar stage, memory-ownership, decode-composition, and validation rules. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the shared superscalar pipeline

Read [README.md](README.md) for the current public contract. This guide owns
the fetch frontend, integer slice, age/order rules, and behavioral fixtures. Follow
[core ownership](../DEVELOPING.md), the repository
[package graph](../../rhodium/DEVELOPING.md), and
[test policy](../../tools/testing/DEVELOPING.md).

## Architecture and ownership

This family owns the complete implementation shared by RV2Wide and RV3Wide:
configuration validation/projection, decode, frontend, pipeline, cache/MMU
integration, and behavioral fixtures. Each named core owns a distinct config
class and reset-started hart circuit. Its `pipeline` field contains the shared
`SuperscalarPipelineConfig`; direct core entry points and UDB use that same value.
All shared parameters, defaults, validation, and derived descriptions live here,
not in copied named-core constructors. Never import named-core policy from production sources
here. Shared fixtures may instantiate the named entry points.

The named issue circuit's signature selects two or three backend lanes, and the
integrated frontend/window uses the same width while keeping eight-byte fetch.
All lanes can own shared services; FP tags and retained descriptor selectors
encode instruction positions independently of physical write ports. Issue emits
unqualified probe owners so service readiness cannot feed back into probe selection;
the admitted prefix qualifies actual launches. WB checks every older lane before
dispatch, retaining age-directed scalar/vector admission to avoid ready loops.
The issue module remains the only prefix-admission authority. Never widen
two-operand bundles, the four physical read paths, or single-service inputs mechanically.

Most family fixtures retain their `rv2wide-*` runner IDs and C++ filenames.
The `rv2wide-fetch` and `rv3wide-fetch` fixtures exercise their respective named
cores with active BTBs and the same instruction oracle. Both use
the existing `rv2wide` cosim observation contract and native reconstruction
adapter because they share pipeline and service ownership. The capture derives
its retirement width from the selected core's taps.

`SuperscalarPipelineConfig.xlen` selects scalar width through decode, pipeline payloads,
prediction addresses, register state, load/store shaping, and integer services.
Use canonical RV32 instruction catalogs rather than projecting RV64 patterns;
compressed expansion and the composed decoder must select the same domain.
Both consume `SuperscalarPipelineConfig` directly through `superscalar_instructions(profile)`
and `superscalar_control_cases(profile)`. Keep XLEN selection in one canonical
integer-domain function, not a second default-RV64 decoder or caller-maintained
feature-keyword list. Optional rows reuse inactive component patterns while
retaining don't-care masks.
Keep fixed 64-bit fetch blocks, boot/time inputs, and CHI transport separate
from architectural width. The shared uncached engine remains 64-bit so it can
return one complete fetch block; scalar IO is explicitly widened/narrowed at
the cache adapter, preserving its exact access size and byte offset.
At the translation boundary, pair the native request with its resolved physical
address and PBMT in `PhysicalDataReq`. Core VAs are XLEN. The physical request keeps
64 address bits through PMA and IO routing, including on RV32. The transport
adds its client tag to `memory.context` and carries the resolved PBMT alongside
the transaction.

`superscalar.rhdl` selects an identity or RV64 Sv39/guest translation
specialization from `profile.mmu_type` at elaboration and passes it directly to
`SuperscalarMemoryAdapter`. `memory/adapter.rhdl` owns the single EX/MEM
context register, physical attribute qualification, split execution, and fault
projection. `memory/transport.rhdl` binds the [shared arbiter](../common/memory-arbiter.rhdl)
to the superscalar client tag, eight-credit limit, and admission faults.
Keep exact SoC profile checks unchanged: `rv32int` additionally selects Zve32x/VLEN64,
Zvbb/Zvkt and the preset's optional scalar/system guarantees. `rv32max` adds
scalar F and Zve32f while retaining the same Bare platform and vector geometry.
`rv64max` projects the complete non-H RV64 preset with VLEN128 and Sv39.
`rva23` adds H/Sha to the same preset. Keep the SoC/UDB mandatory-requirement
checks and ordinary Mini/Simple cosim lanes aligned with that exact selection.
`profile.execution_half_precision` selects shared FP hardware for the union of
scalar and vector capabilities; scalar decode must continue to use
`profile.half_precision`. The core-vector fixture exercises scalar Zfhmin with
full Zvfh, including half arithmetic, width conversions, flag completion, and
scalar/vector FP contention.
The shared [`fp_transport_profile(xlen)`](../common/fp/profile.rhm) selects the native pipeline transport shape (RV32F
or RV64D), including the disabled adapter. Do not put 64-bit FPR payloads in
RV32 pipeline tokens. The raw FP-load transport remains 64 bits because the
shared boxing helper consumes a width-independent load image.

Keep stage logic in one pipeline-ordered `core.rhdl`. Flow owns feed-forward
storage; the issue window owns retention and compaction. The supplied named
issue circuit owns prefix admission; `issue.rhdl` owns its shared contract and predicates.
No independent lane handshake may allow a younger instruction to pass an
older blocked instruction. WB is the only transaction and retirement authority;
its accepted deferred owners later write through the shared last-lane port.
Redirect qualification gates new transfers as well as flushing pipe state.

`read-operands.rhdl` owns bounded four-path scalar read routing, forwarding,
operand selection, and immediate computation, not admission or register-file storage.
RR applies `superscalar_normalize_move` before decode and source extraction;
issue candidates and execution controls must describe the same normalized word.
Keep the original expanded word, `raw_instruction`, and sequential PC untouched
in fetched identity for retirement, cosimulation, tracing, and trap values.
Compact lane storage reconstructs the raw upper half from that expanded word.
RR captures `OperandImmediate.Zero` for rewritten copies so EX never interprets
the original ADD source bits as an ADDI immediate. This local
superscalar optimization does not change architectural compressed expansion.
Feed its read-routing helper candidate presence and
decoded source-use bits, never final issue grants or forwarding availability.
In the three-candidate specialization, lane 3 prefers lane 2 rs2's path for rs1
and lane 1 rs2's path for rs2 (one-based numbering). First compare both sources
against all four active older reads, choosing the lowest matching path. Equal
third-lane sources share a single allocation. A sole unmatched source can fall
back to the other unused rs2 donor; two distinct unmatched sources keep their
fixed routes. Older rs1 paths may be shared but never reassigned. Express this small demand-only
choice directly, without serial allocation or readiness feedback. Read denial
is a structural hazard, separate from unavailable data on an allocated path.
Do not free a donor for a same-group replacement route in this first cut.
`superscalar_issue_reads` may instead select an older move's existing physical
path, including for either third-lane operand. This is source sharing, not a new
read allocation or EX result chain. Scan nearest writers in program order and
retain each move's source route at its position. A non-move dependency cannot
become ready by passing through a move; source overwrites after a move must not
retarget its consumers. These selectors and their readiness share one route.
The core evaluates deferred-return forwarding, interstage forwarding, and
scoreboard readiness once for each selected physical address. Only the four
readiness flags feed issue; data stays in the register-read datapath.
`superscalar_prepare_operands` applies issue's instruction bits and mux controls.
Do not move bypass comparison ahead of address selection
or construct six independent forwarding networks. Preserve oldest-to-youngest
producer priority, including unavailable/replaying writers hiding older data.
Named `RV2WideIssue` and `RV3WideIssue` use these helpers and shared dependency
predicates, but each owns its complete prefix grant. RV2Wide explicitly checks
one pair; RV3Wide scans three candidates and arbitrates borrowed reads. Each lane owns a fixed write port.
Core RR supplies decoded candidates, registered service views, and
physical-path readiness; it applies the returned mux controls and grants without another issue
policy. Keep scoreboard state, FP latency queries, branch-training occupancy
projection, vector ordering state, and actual calendar bookings in the core.
The GPR file has four read ports and one write port per lane: 2W on RV2Wide,
3W on RV3Wide. Issue carries no write routing or coalescing metadata. WB compares
destinations and suppresses an older write only when a younger lane actually
writes successfully. Fault/replay/deferred younger owners never suppress older
state, and retirement records preserve every instruction's own result.

The last port also serves accepted delayed load/divide, multiply, late FP, and
vector scalar returns. The completion calendar reserves the last issue lane
before its WB edge, including nonwriters; the earlier prefix can continue.
Book ordinary last-lane admission, not lane 2 unconditionally in the three-lane
specialization. Preserve response fairness, scoreboard ordering, and accepted
owners across speculative flush. Retained retirement still normalizes its owner
to slot zero and may merge one scalar into slot one; on RV3Wide that merge does
not compete with the return port. Keep this normalization separate from live
pipeline lane identity and check write-port exclusion at actual WB.

`SuperscalarResult.branch` groups comparison, recovery, and training state. FP
requests associated with a fetched instruction carry only `SuperscalarFpSchedule`;
attach the complete `SuperscalarFpOwner` at EX launch, WB authorization, or
delayed-memory allocation. Do not store a duplicate fetched instruction in the request tag.
These are ownership boundaries, not extra pipeline stages or latency assumptions.

EX, MEM, and WB each store one selected FP request for the group, not one per
instruction. Pair admission allows only one scalar FP operand/control owner.
The older transfer captures the shared request even when its owner is younger;
all lanes consume it at the service boundary without local payload storage. Lane validity and the
existing dispatch prefix still control launch and authorization. Killing a
younger owner does not stall the group register or make its request valid for
the older instruction. Bubbles need no payload reset or extra valid bit. Keep
the three stage banks independent so adjacent FP groups retain full throughput.

Only EX stores the full `FpExecutionRequest`. The canonical MEM/WB record is
`SuperscalarFpRetirement`: scheduling/destination, precision, operation, and
rounding metadata, with no numeric operands. Fixed work already launched at EX
and needs only authorization. Delayed and split FP loads retain their
architectural owner independently.

Each MEM lane owns two `SuperscalarResult.operands` words.
Branches, integer divide, and vector configuration use the GPR pair; FP
divide/sqrt takes the FP pair. A CSR uses the left word (rs1 or zero-extended
immediate), and scalar memory uses the right word (including FPR data for an FP
store). Select on the individual instruction, not the group's FP request:
a paired branch still needs its integer operands in MEM. After branch comparison,
`for_writeback()` drives the WB address and left operand from one XLEN word.
Memory operations, prefetches, and all branches (even not-taken ones) keep the
address; other instructions keep the left operand. At MEM→WB, at most one
surviving scalar service needs the right word. Capture it once per group for
memory (including the passive observer and retained split stores), integer
divide, or FP divide/sqrt. WB validity and authorization remain per instruction;
replay reacquires the operands on reissue. A register-supplied `vsetvl` can
pair with a scalar service. Only vector-enabled builds capture its `vtype`
operand separately; immediate forms take their bits from the instruction.

MEM/WB lane results and retirement commands each carry a `SuperscalarResolution`
field. Its single XLEN `value` word holds either normal result data or the
exception value. When applying a new resolution, `.with_result(data)` keeps
the exception value for `Fault` and uses the supplied result for every other disposition.
The result and resolution consumers read that same word; consumers
must qualify their use. A fault cannot forward or retire result data, and its
exception value is meaningful only for `Fault`. MEM may replace a successful
result with a load value or a newly resolved fault before the WB capture. Earlier
faults retain priority. Keep the value independent of the shared WB word; do
not narrow exception values or discard cause, guest provenance, or fetch-fault
origin. The lane banks retain their existing validity, flush, and cycle boundaries.

The compact `SuperscalarExecute` and `SuperscalarResult` are the canonical
pipeline payloads. `lane-pipe.rhdl` owns one flushable `ValidPipe` per lane
and exposes scalar PC/successor views from the stage-owned prefix. FP requests
stay in the group banks; consumers join them only at service boundaries.
Do not introduce stored/full bundle pairs or materialize a whole record merely
to expand a PC. Add fields deliberately to their actual storage owner.
`SuperscalarRetirementCommand` is a narrower, combinational retirement/CSR
projection, not an expanded pipeline clone. It omits execution comparison,
arithmetic-launch, cache-outcome and guest-access routing state. Retained
services produce this command directly; new dispatch still reads live WB.
Select the retirement command and PC from the same logical slot: a returned
owner replaces slot zero, and a merged scalar in slot one still uses its live
source lane’s PC. The youngest successful slot supplies the resume PC at an
empty-pipeline interrupt, even when it is the third slot across a page boundary.
Build delayed completion identities from the shared lane instruction and the
stage's full PC. Derive the resolved next PC and project the trap/observation
boundary instruction on the retirement command, where both live and retained
paths already meet.

At RR→EX, EX→MEM, and MEM→WB, [all lane pipes](lane-pipe.rhdl) store only
the low 12 bits of their starting PCs. Each stage owns one shared upper-PC
register and one next-region bit per younger lane, captured on the oldest lane's transfer
even when it issues alone. Each younger lane selects that prefix or its modular
successor; the older lane always uses the unmodified prefix. Recovery may kill
the younger lane alone, so it must not gate prefix advancement. A bubble or
flush needs no prefix reset or separate valid bit. The prediction PC, EX's
vector PC, and the MEM/WB branch-update PC all alias the starting PC. The
branch return address aliases the sequential PC. Prediction targets are
independent; never reconstruct them from the group prefix.
Use the reconstructed PC for traces and before handing work to any independently
retained owner. The issue window and a merged WB retirement group do not have
this prefix invariant. RR admits only same-region or next-region groups; backward
and more distant regions remain separate issues. The relative bit advances with
the shared prefix through EX/MEM/WB, not with a potentially killed younger lane.

MEM and WB each retain one vector execution descriptor for the group. The
ordinary lane pipes carry only the vector classification needed for hazards,
retirement, and dispatch. A continuing vector owns the descriptor; its lane
tag advances with the same surviving transfer as the scalar token. A younger
fetch fault can bypass normal pairing but cannot claim the payload. Each stage
also forwards one successful vset result independently: a vset and a vector
can pair, so their state snapshots are not interchangeable.

Delayed-memory context carries PC/raw encoding, destination identity, return bank,
byte offset, and load formatting through memory. Four scalar credits bound
outstanding work; returned context selects the completion port without a shadow
owner queue. Format ordinary loads by width and offset; atomic results already
contain their architectural value or status.
Multiply, divide, FP and vector GPR returns use the same compact instruction
identity. Keep the full fetch record on retirement, not in these return tags.

Completion pipes, FP tags and arithmetic owners use the same fixed-PC
`SuperscalarInstructionIdentity`, `SuperscalarFpOwner` and
`SuperscalarCompletion` as their consumers. Their PC is independently
sign-extended with `.address()` at architectural observation boundaries, never
reconstructed from a live pipeline prefix. Admission establishes exact successful
PC representability. Trap owners retain full addresses. Width changes do not
alter latency, capacity, raw instruction ownership or accepted-work flush survival.

Instruction producers encode the sequential PC once when constructing the canonical packet. Adding
two or four toggles a contiguous run of bits, starting at bit 1 or 2.
Store the highest toggled bit and whether bit 1 toggled (six bits on RV32,
seven on RV64). Forward this tag unchanged through the lane registers. The
window heads and stage outputs apply the toggles to their own PC; they need
neither a second offset nor a PC adder. This also handles 4 KiB crossings,
XLEN wrap, and faulting instructions whose raw bits do not identify their
length. Preserve the valid prediction target, direction checkpoint, and RAS actions
even when an instruction crosses a block or region.

Frontend group width changes only packet capacity, assembler prefix scanning,
and visible/consumable window entries. Fetch blocks remain eight bytes, assembler
storage remains three blocks, and the issue window remains eight entries. The
assembler examines at most two contiguous blocks; if a third instruction needs
a third block, emit the complete prefix and retain its suffix for the next cycle.
The ordered scan ends after a fault or taken prediction and before a second RAS
action. Select repair history from the accepted tail, and qualify all prediction
and RAS effects with the packet transfer. Keep block trace ancestry separate
from instruction count: a three-instruction packet still has at most two block
parents. Window trace views replicate accepted packet ownership for each of its
appended instructions and consume only the caller-selected oldest prefix.
Do not widen two-operand bundles, retirement feedback, or backend stage state
when changing frontend group width.

The issue window and each EX/MEM/WB lane store `SuperscalarDirectionCheckpoint`,
not complete tournament indices. Its ten-bit saved lookup row, ten-bit recovery
history, and four flags are independent. Reconstruct `index` by concatenating the
row with `pc[1..3]`, and `base_index` from `pc[1..13]`. Use the lane's full
reconstructed PC, including the shared region bit needed at a 4 KiB boundary.
No index adder or history hash is needed. Window and lane admission check valid
index/PC agreement; invalid indices have no training meaning. Preserve histories
and flags even when direction validity is false, since recovery still consumes
nonbranch checkpoints. Instruction packets retain the checkpoint directly; only predictor training and test observation construct complete indices.

Each window entry stores its own `CompactPc`, not an offset from a shared base:
resident instructions can span unrelated predicted targets. Successful PCs
sign-extend from 39 bits for RV64 or retain all 32 bits for RV32, in standalone
and integrated cores alike. The frontend checks representability after translation
and before physical fetch admission, including guest-Bare. Integration validates
every executable address-set maximum against the Bare limit; data regions are independent.
`SuperscalarInstructionOutcome` overlays a valid/target prediction with a parcel
fault and a positive sequential-boundary bit. Faulting instructions cannot predict.
The window and EX/MEM/WB lane registers retain the fixed compact-PC
width and sign-extend it for consumers. Never narrow architectural
redirects or the resolved target. Each window entry owns its metadata.
The assembler, window, retained cursor predictions, and lane prediction targets
use shared `cores/common/pc.rhdl` capture/expansion semantics. Exact-PC admission uses
`CompactPc.fits`; speculative destinations use `CompactPc.prediction`.
Boundary faults zero-extend their low PC instead of sign-extending it. Direction-prefix block tags
and the lane pipes' shared-region PC suffixes keep their existing widths and
ownership; join a direction block's fixed byte offset before sign expansion.

Window and lane registers retain the canonical instruction and the raw low
halfword. Decode uses the canonical word directly. Restore the original upper
half from the canonical word only when the raw low bits are `11`; otherwise
it is zero. Use the raw parcel, not the successor distance or legality, for
this decision so traces and illegal-instruction trap values stay exact.

On consume, a surviving window entry moves at most `group_width` rows toward
the head. Wire only the in-bounds neighbors for each destination. Appends
start at the remaining count, clear discards the old owners, and zero consume
holds every survivor. The oldest entries remain directly wired to decode.

At the assembler input, shared `FetchBlockFault` from `cores/common/fetch/fault.rhdl` reports the exact failed
block VA. At packet formation the assembler asserts that it matches the
instruction PC, or PC+2 modulo XLEN when a straddle's second parcel failed.
From there shared `FetchFault` carries validity, the shared `ExceptionCause`,
continuation, and (with H) guest provenance. Retained assembler and instruction
union payloads omit validity because their `Fault` tag already establishes it.
No fetch-specific cause encoding or cause conversion belongs on this path.
EX reconstructs the VA from the instruction's exact PC when
forming the trap value. MEM and WB retain just the fetch-fault
origin bit beside the resolved outcome. Preserve EX's fetch-fault priority
and MEM's earlier-fault priority: the observed fetch fault is reconstructed
from that resolution when the bit is set, otherwise it is all zero. Do not
label a later data or CSR fault as a fetch fault. The vector core fixtures
exercise fault-address wrap, canonical absence, and H guest-page-fault CSRs;
the fetching fixture covers continuation-page faults.

Carry the shared `ExceptionCause` enum through MEM/WB lanes, retained WB
owners, vector outcomes, and CSR commands. Do not duplicate cause widths or
slice architectural numbers into pipeline fields. Pack only at CSR and public
observation boundaries. Interrupts remain separate from synchronous causes;
the combinational redirect's `trap_cause` reports the combined architectural
image without adding pipeline storage.

WB separates live/retained owners, the pre-dispatch prefix (`wb_dispatch`),
service acceptance/faults, and the successful retirement prefix (`wb_retire`).
Dispatch must not consume final retirement validity: admission readiness/faults
would then feed back into the request that produced them. Retained split/CMO/WRS
state remains local to WB and accepted work survives speculative flush.
Retained vector authorization uses the same owner storage without continuously
squashing the scalar pipes; only blocking retained operations retain that policy.
The retained lifecycle is `Idle -> Draining -> Ready -> Accepted -> Idle`;
completion can exit an earlier phase for a reservation wake or admission fault.
Keep capture/completion priority and the registered drain edge unchanged.
`SuperscalarServiceOwner` retains only the instruction/observation metadata,
destination, memory operands/control, guest-access mode, wait selector, FP-load
schedule, and vector classification. It carries the selected lane's existing
successor tag rather than a second full PC or newly encoded offset. Capture
requires a continuing, nonbranch service with no CSR, arithmetic, hint, or
non-memory FP operation. Completion supplies fresh data and fault resolution
directly into `SuperscalarRetirementCommand`; it never reconstructs a pipeline
record or reissues execution units.
Preserve the saved prediction-repair bit and metadata even for nonbranches,
including stale predictions. Split FP loads still need their original FPR
destination, precision, and scheduling tag. The functional lifecycle and
passive retirement/event interfaces remain unchanged.
Use Flow maps/filters for combinational stage transfers; explicit selection and
retained-storage contracts remain where core policy chooses or retains owners.

Reuse ALU, branch, load/store shaping, and shared scoreboard components and the `cores/common/*-decode.rhdl` instruction
relations. Never import RV5Stage or another named core. Do not create an
instruction-kind enum followed by a second runtime control decoder.
Core control rows join exact canonical instruction patterns using
`component_output`, following RV5Stage's composition pattern. Unobserved
payload controls stay don't-cares behind cared enables/source-use bits.
`tests/decode-test.rhm` checks these masks across XLEN, FP/vector, and guest
rows. Only ordinary ALU results and jumps constrain the EX writeback `link`
selection; other result owners overwrite it. Guest-access and side-effect
discriminators remain explicit even when their payloads are unused.
The selected B catalog joins `RV32BAluCases` or `RV64BAluCases` into each decoder.
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
replicating a packet owner for each appended instruction. The assembler
selects the one/two retained or live block owners contributing bytes to each packet.
EX/MEM mapping and lane grants certify manually authored combinational regions.
Split ownership uses its actual capture/release/pending controls. Successful WB
parents accepted load/divide requests; EX parents the multiply launch marker.
WB authorization filters killed launches and attaches the surviving instruction's
completion metadata. Vector admission has its own live-WB fork branch and
pending-safe lane-selection contract, so stalls retain the offered owner's
ancestry too. Its descriptor queue
and sequencer retain that MEM occurrence, independently of eventual retirement;
never qualify vector admission with final WB retirement feedback. Do not add a whole
module trace contract across already contracted Flow children. The shared divider
owns its intrinsic retained request-to-response contract.

Retirement prediction captures derive from the WB owner's retained resolved
successor and effective prediction. Keep next-PC comparison separate from the
RAS-action comparison; the recovery flag intentionally combines both and is not
an accuracy metric. Do not infer retirement or prediction results from matching
PCs in the host. The instrumented `rv2wide-fetch` oracle compares both slots'
PC, raw instruction, prediction result, and RAS mismatch after settled callbacks,
using its independent instruction execution and public prediction payloads.

### Supervisor and guest context

Shared `RiscvCsrFile` owns Ssnpm, Sscofpmf, state-enable, and H architectural
state. Superscalar only projects configuration, counts the successful retirement
prefix, and supplies precise commands and faults. Apply pointer masking after
the EX address bypass, before lookup/alignment. Serialized CSR updates flush
younger work, so no stale masking mode survives a context change.

Guest access is a composed decode column, not an opcode re-decoder. Retain it
through MEM/WB and split ownership. The MMU builds one shared lookup shape with
both VS/G roots, SUM/MXR/PBMT controls, effective privilege, and HLVX intent.
Keep ordinary MPRV/MPV separate from explicit HLV/HSV context. Full invalidation
serializes at WB; never cancel an accepted physical response owner.

With H, fetch faults and resolution payloads carry `RiscvGuestFault` through either
age slot. Preserve second-fragment VAs and implicit-PTE GPAs. Physical faults
also retain whether the address was guest virtual. The cosim adapter consumes
shared passive virtual-context, trap-provenance, and HPM timing observations;
it does not implement a second CSR model.

Extend the existing `rv2wide-core` and `rv2wide-mmu` fixtures for these
boundaries and rerun `rv2wide-fetch`/FP integration when packed payloads change.
Profile/UDB tests cover selected declarations and implementation parameters.
Keep broader ISA qualification in the existing software suites.

## Implementation map

Keep the public composition, execution slice, configuration, common bundles,
and execution helpers at the package root. `fetch/` owns block fetching,
instruction assembly, and fetch-specific prediction refinement; `memory/`
owns scalar/vector port arbitration, translation specialization, physical
transport, and the L1D/uncached attachment. `decode/` owns instruction-control
relations. Shared cache engines and predictor tables remain in `../common/cache/`
and `../common/bpred/`. These directories retain this guide as their contributor guide.
Family-level fixtures stay together under `tests/`, including those that
exercise several of these owners.

`../common/reservation-wait.rhdl` supplies the WRS timer and wake/timeout result;
the core retains the waiting WB lane and qualifies TW/VTW. Scalar M-extension
operand/result formatting uses `../common/integer-format.rhdl`; RR/EX reservations,
WB authorization, and the two-port writeback policy stay local.

| Owner | Responsibility |
|---|---|
| `decode/operand-ctrl.rhdl` | Composes shared I/M/A/B/Zicond operand relations with Superscalar MOP routing policy |
| [`../common/memory-control.rhdl`](../common/memory-control.rhdl) | Scalar LSU controls. EX sets `memory_active` on executable memory instructions and preserves the decoded operation for fault classification. |
| `decode/system-ctrl.rhdl` | Architectural CSR operation/source, trap/return/wait, and serialized fence columns |
| `decode/long-ctrl.rhdl` | Shared multiply/divide relations, service selection, inactive care masks |
| `decode/core-ctrl.rhdl` | Selected instruction domain, writeback column, one combined relation |
| `bundles.rhdl` | Instruction, lookup/admission/response, stage, and retirement contracts |
| `../common/fetch/protocol.rhdl`, `../common/bpred/protocol.rhdl` | Shared physical fetch request and next-PC prediction status; core timing and retirement qualification remain local |
| `issue-window.rhdl` | Sole eight-entry compacting instruction buffer, free-entry count, prefix consumption |
| `issue.rhdl` | Shared issue contracts, signature validation, and dependency/resource predicates |
| `branch-training.rhdl` | Two-entry, two-offer/one-write retirement training buffer, empty bypass, predictor clear |
| `pairing.rhdl` | Same-group recognition and control-only RR/EX/MEM mux selections |
| `read-operands.rhdl` | Four physical read/bypass paths, issue-controlled operand muxes, and immediate computation |
| `fetch/frontend.rhdl` | Credited block fetch, S1 translation/permissions, local replay, and block fault ownership |
| `fetch/direction.rhdl`, `fetch/predecode.rhdl` | Fresh-S2 direction refinement, retained continuation prefixes, and shared frontend/assembler instruction classification |
| `fetch/instruction-assembler.rhdl` | Flow block storage, mixed-width parcel consumption, shared C expansion, and continuation faults |
| `../common/fetch/memory.rhdl` | Shared fixed-cycle L1I/noncacheable outcomes, retained fetch replay, and cancellation |
| `core.rhdl` | RR/EX/MEM/WB, forwarding, shared component/CSR instances, register state, precise traps |
| `memory-completion.rhdl` | Four outstanding scalar credits and context-directed load formatting/writeback projection |
| `long-execution.rhdl` | EX multiply reservations and WB owner validation, retained divider ownership |
| `memory/attachment.rhdl` | Physical permissions, shared L1D adaptation, and ordered IOMSHR/uncached routing |
| `memory/translation.rhdl` | Combinational query/result contract, including known/fault provenance, probe contention, and held split masking |
| `memory/translation-specialization.rhm` | Immutable circuit-construction contract, architectural width, physical map, walker-client capability, split-access policy, and atomic PMA policy |
| `memory/adapter.rhdl` | EX/MEM timing, physical PMA/PBMT qualification, split owner, committed requests, faults, and passive physical observations |
| `memory/transport.rhdl` | Physical client tag wrapping and removal, eight-credit configuration, and live-grant admission faults |
| `memory/translation-identity.rhdl`, `memory/translation-sv39.rhdl` | Concrete specialization objects and their identity versus TLB/walker circuits, vector proof services, and prefetch timing |
| `fp.rhdl` | RR fixed-return bookings, EX arithmetic launch, WB authorization, FPR hazards and load/arithmetic write ports |
| `vector.rhdl` | WB-authorized composition of shared admission/execution and the local LSU adapter |
| `vector-memory.rhdl` | Adapts native lookup, slow and split admission, and raw-word extraction to the shared vector memory controller |
| `memory/ports.rhdl` | Scalar/vector lookup, accepted response, and split-owner arbitration |
| `superscalar.rhdl` | Frontend/core/shared L1I/L1D composition, profile-selected translation specialization, distinct CHI identities, start/halt boundary |
| `profile.rhm`, `udb.rhm` | Scalar RV32/RV64 architectural description, shared RTL/metadata CSR specialization, and implementation-owned UDB choices |
| `../rv2wide/hart.rhdl`, `../rv3wide/hart.rhdl` | Named-core SoC port adaptation and one reset-vector start per reset epoch |
| `tests/rsim/` | Direct C++ architectural, memory, prediction, and ordering oracles |
| `tests/rsim/rv2wide-fetch.cpp`, `tests/rsim/rv2wide-rv32.cpp` | Production fetching/cache top, retirement tracing, and RV32 integration |

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
Do not allocate a deferred-completion credit/scoreboard owner or reuse the admission-certified
aligned response port. Interrupt entry waits for this noncancelable owner.

`cores/common/misaligned-access.rhdl` owns fragment masks, store positioning, and
load assembly; this MMU owns each fragment's translation/PMA check. Fragment
responses, ordinary core replies, and PTE responses carry a physical-client tag
through memory and are routed with `demux_flow`. An eight-entry credit counter
preserves the previous admission bound without retaining a parallel routing FIFO. The second fragment must
wait for first-fragment completion, and fault provenance must use the engine's
virtual fault address, not its aligned physical request. Preserve accepted
prefixes on later faults. Check the full XLEN/8-byte footprint against normal,
cacheable, idempotent memory; do not widen device accesses into this path.

Preserve natural access width through MMU translation. Cache service aligns its
own beats; uncached service keeps the exact address/size and unpositions store
data for the shared StoreGen. The IOMSHR retains the caller's opaque owner and
three-bit byte offset locally, restoring a returned scalar to its raw-beat lane before
returning the original context to the core's LoadGen. Do not issue
speculative physical requests to devices or use widened PMA checks that reject
legitimate sub-beat regions. Page-table traffic still requires cacheable RAM.

The IOMSHR admits only after L1D drains; its registered busy state gates cached
admission and speculative physical resolution. Carry that state back through
the MMU as `ordered_busy`: WB must replay even an earlier MEM hit while IO is
outstanding. Gating only slow requests misses that race. Neither the core nor
speculative flush cancels an accepted IO owner. The physical response paths are
mutually ordered and merge using Flow arbitration, preserving returned context.

Keep accepted ownership independent of speculative flush. Scalar requests carry
their instruction/destination/format context through memory; a four-entry credit
counter tracks capacity until response consumption. The completion projection
uses the returned context and `LoadGen` for ordinary loads, passes architectural LR/AMO values and SC status
unchanged, and backpressures at the common completion arbiter. Round-robin Flow
arbitration merges variable load and divide results. When RR accepts a return
that writes a GPR, it leaves the last issue slot vacant. The return then uses
that slot's XLEN register in each stage: the first source operand in EX (also
the vector scalar), and the result or fault value in MEM and WB. Only the word
is shared. Each stage advances the committed return's validity and metadata
independently of the instruction's flushable validity, so an older return
survives a younger instruction's redirect.

The register file has four physical reads and one write port per issue slot.
Earlier slots can still write while the return uses the last port. Successful
same-address normal writes suppress the older write without merging retirement.
Do not add a return-only write port or backpressure WB. The completion-pipeline
occupancy participates in precise fault drain.
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
must not suppress the older write. The last port can carry an older completion,
so never infer younger-write priority from the port number. Insert direct FP
returns into their WB lane's forwarding payload; late FP returns precede the
pipeline's age-ordered producers. An older FP bypass must not override a younger writer.

The RR datapath forwards an immediate-only ordinary adder's constant to either replaced GPR
source. LUI and ADDI/ADDIW from x0 need no EX result. Use the substituted capture
values consistently for scalar operands, the FP integer request, and vector
scalar/stride snapshots; no consumer may bypass its interlock yet capture stale RF
data. Keep the ordinary producer eligibility, destination reservations, and
unrelated source checks. Nonconstant ALU chains still interlock.

For AUIPC+ADDI or a full-width immediate adder feeding a scalar load/store base,
The RR datapath folds the producer and consumer immediates and selects the producer's
original register base or PC for younger rs1. `operands.rewrite.immediate` carries the folded
sum separately from rs2: a store still needs its original data operand. The sum
uses 33 signed bits on RV64 and wraps at 32 bits on RV32. `rewrite.composed` selects
that immediate at EX; the two ordinary ALUs remain independent. Preserve both
architectural instruction records and the older prefix on a younger rejection.
Nonconstant word-result producers cannot compose addresses because their
intermediate truncation/sign-extension is observable.


`pairing.rhdl` selects one internal `SuperscalarPairRoute` per younger source.
Its `SuperscalarPairing` value is the sole authority for readiness exemptions,
unresolved dependencies, and mux controls inside each named issue circuit.
`SuperscalarIssueInstruction` carries unchanged instruction bits, decoded controls,
and `SuperscalarPairControl` selectors. No register data or computed immediates
cross this boundary. `read-operands.rhdl` applies the selectors without pair
recognition; its `SuperscalarOperands` result is captured in EX. Only later-stage
controls and the composed immediate survive in `SuperscalarExecutionRewrite`.
Keep resource conflicts, deferred WAW checks, and prefix admission in the named
issue module; resource state and bookings remain in `core.rhdl`.
A route never overrides those interlocks. Classify consumers with decoded
controls and displacement. Immediate-adder classification checks the physical
adder transforms; the retained AUIPC/ADDI rewrite checks its exact forms.

For scalar memory, RR may waive the younger rs1 RAW/read interlock for a zero-displacement
load/store (including FP memory and HLV/HLVX) whose base is the same-pair older non-memory,
non-M, non-FP, non-system, non-branch writer. The older writer still passes every
ordinary admission interlock.
Carry `rewrite.memory_address` across the existing EX register; select the
immediately preceding lane's ALU result directly as the younger address, after
rather than before the younger ALU. RV3Wide recognizes the lane 2→3 route
independently of the full lane 1→2 pairing machinery. It replaces only rs1's
RAW/read-allocation/readiness checks; the nearest writer supersedes earlier
rs1 matches. Do not waive store-data or destination hazards. Never route this
bypass through another dependent addition. Keep normal
MEM checks, WB authorization, split-access ownership, and fault/replay priority.
Every other source and destination hazard retains its ordinary interlock.

The same ordinary ALU producer may supply a younger integer store's rs2.
Waive only that source's RAW/read interlock and carry `rewrite.store_data`
through EX. Select the older ALU result before StoreGen and retain the raw value
in the lane's MEM operand and then the shared WB service word, including for
slow and split accesses.
The store's rs1 dependency needs its own constant, composition, or address route;
store-data forwarding alone does not waive it or restrict its immediate offset.
Producer admission, WB store authorization, and oldest fault/replay priority
remain unchanged; FP stores, atomics, and deferred producers do not use this path.

For a younger conditional branch, RR may waive either comparison source's
RAW/read interlock when the same eligible older ALU writer replaces it. Carry
the selected `rewrite.branch_operands` mux enables through EX and into
MEM, alongside the original operands and shared `BranchResolverControl`. MEM selects slot
zero's registered ALU result before the younger slot's comparator. Do not add
an EX ALU-to-comparator path, waive an unrelated source, or extend the selection
to deferred/FP/system/control-transfer producers or a dependent JALR base.
EX still calculates the PC-relative or JALR target independently. MEM resolves
direction, target alignment, effective successor, RAS mismatch, and tournament
outcome before its existing oldest-stop qualification and WB transfer.

Admission is conflict-based, not a list of permitted instruction combinations.
Keep GPR/FPR dependencies, resource conflicts, scheduled return capacity, and
serialization distinct. Scalar memory, integer M, and variable FP share one
scalar WB admission path; vector descriptors have a separate single input.
Duplicate claims on either resource split at RR, not every pair of services.
WB orders scalar/vector admission by age. `wb_scalar_prefix` uses independent,
registered non-pipelined VDQ credit and nonretaining eligibility to qualify
scalar requests behind an older descriptor. `wb_scalar_stop` folds scalar
rejection, fault, retention, and memory ordering before offering a younger
descriptor. Neither direction consumes final retirement feedback. Keep scalar
capture/replay independent of vector capture to preserve this acyclic ordering.
Older rejection suppresses every younger service effect; younger rejection/fault
preserves the accepted older instruction and recovers only the unsuccessful
suffix. Include the older same-group vector memory operation in scalar memory
ordering summaries; a younger vector memory descriptor waits for same-group
scalar memory to drain. Do not add instruction-pair permission predicates.
Two configurations contend for the CSR command; two FP operand/control owners
contend for the selected FPR read bundle; RAS actions and fixed return collisions
retain their existing capacity checks. Do not recreate permitted-pair predicates
when adding an instruction; add only the resource or ordering conflict it creates.

Canonical scalar-use metadata separates VRF and GPR namespaces. A younger ALU
may overwrite a vector's scalar source because the descriptor snapshots the
pre-write value. Same-pair FPR RAW/WAW checks include vector scalar-FPR results
and scalar FP operands/destinations, including f0, even when their shared read
bundle does not conflict. Bypass routes remain explicit physical capabilities;
a route may waive only the source dependency it actually supplies.
Each lane uses its existing EX target and MEM comparator. Older WB rejection
overrides speculative branch correction and prevents younger retirement/training.

For younger non-memory vector arithmetic, the ordinary ALU producer may
replace a genuine integer rs1 dependency with `VectorScalar`. This route waives
only that rs1 RAW/read interlock.
EX captures slot zero's ALU result into the younger descriptor's existing
`scalar` field at the EX/MEM edge. Leave the younger scalar ALU operands and
vector memory base/stride and footprint logic unchanged; there is no dependent
second ALU operation or new storage stage. Canonical scalar-use metadata keeps
immediate and VRF operands from being mistaken for GPR dependencies. Producer
admission, destination hazards, calendar reservations, and WB's successful
retirement/dispatch prefix remain authoritative. Do not extend the route to
load/M/FP/system/control-transfer producers or dependent vector configuration.

M instructions use the shared physical control relations in the same composed
decoder. Only one memory-or-M deferred destination may issue per group, matching
the one scoreboard set port. Long operations disable ALU forwarding until their
completion is available for forwarding. x0 M results need no service owner.

EX launches the three-stage multiplier. Only scalar launch validity travels
beside the arithmetic for the two cycles to WB; do not duplicate the instruction
identity there. EX selects the product half in a scalar-only build. WB supplies
PC/raw encoding, destination, and word formatting from the surviving instruction;
it also selects the scalar half when using the vector-capable full-product unit.
Only an authorized scalar result can complete. Vector tags accompany the arithmetic result.
RR books EX+3 (RR+4) in the shared fixed GPR calendar.
The authorized product writes and forwards directly on that edge. A killed,
replayed, or faulted owner drains without publishing the speculative product;
its unused booking simply ages out. Keep the multiplier feed-forward, with no
result queue or extra return stages. The outstanding-owner count participates
in precise drain, not capacity admission. FP bookings have first-client priority;
same-group multiply/FP claims for the same edge split at RR before either
executes. Other fixed pairs can book independently. RR uses the calendar's
`registered_free` queries for FP, multiply, and delay-three ordinary returns;
these must not depend on live reserve handshakes. The delay-three query blocks
last-slot issue and variable-return admission for their future WB edge.
Pause new fixed bookings after bounded variable-return starvation. Returns share
the last lane's write port without an extra completion port or WB backpressure.

Division captures its operands and owner only after WB acceptance. A busy service
causes preacceptance replay, while an accepted operation survives all speculative
flushes. Word operations normalize both inputs before division and sign-extend the
selected low word afterward. Precise drain includes multiplier reservations,
divider ownership, load owners, current accepted requests, and completion writes.

A MEM prediction correction clears the instruction buffer, RR, and EX, including EX
lookup admission, while preserving its own MEM-to-WB transfer and older WB work.
An older mispredicted instruction suppresses its same-group younger MEM transfer;
a correctly predicted taken branch preserves its target-stream peer.
Recovery selection observes all lanes' resolved MEM outcomes, including stale
predictions on an ordinary third-lane instruction. WB faults/replays
and retained faults override MEM recovery; branches never redirect twice.
An older same-group memory instruction may reject WB authorization after MEM
branch recovery; its WB redirect must override that speculative target and
suppress the younger branch's retirement and predictor training.

### Predictor ownership and recovery

`fetch/predecode.rhdl` centralizes compressed expansion, control-flow legality, RAS
hints, and immediate offsets for both S2 and assembly. `fetch/direction.rhdl` walks
fresh S2 parcel boundaries, carrying a byte-six prefix independently of the
assembler. The shared tournament component owns only counters; the frontend owns speculative
and committed ten-bit history. S1 registers the PC/history hash, forwarding an
older same-cycle S2 history update. S1 captures PC-indexed bimodal bits before
S2 selects them against gshare using the PC-indexed chooser. A replayed attempt
reuses its saved row and bimodal bits. A byte-six prefix retains its own bimodal
bit across continuation lookup and table training.
Rows are block-based; intra-block checkpoints still append outcomes in order.

The direction walk retains a byte-six prefix as its first halfword, saved row,
bimodal bit, and compact block address. The fixed compact width defines its
successful-PC bound; restore the fixed `110` byte offset and sign extension
before comparing the full XLEN continuation address. Faulting blocks cannot
allocate this state. Standalone direction walks use the same fixed domain.
Qualify a retained prediction at capture with the existing `provisional_prefix`
predicate: it must name this prefix and be uncompressed. Store only that valid
bit and fixed-width target bits; its PC comes from the prefix and its RAS
action is decoded from the completed instruction. Width assertions apply only
to captured successful prefixes and qualified targets. Replay holds all prefix
state, while clear wins a simultaneous capture. Do not narrow continuation
comparisons or architectural fault/redirect addresses.

Direction corrections preserve the fresh S2 block and cancel only younger
lookup contexts. Do not gate current block publication with its own correction.
Metadata for each halfword start travels through block storage; the assembler
selects it by instruction PC and uses the continuation's prefix metadata for a
straddle. Assembly repair restores its accepted prefix, while fetch replay
preserves predecode prefix state. MEM emits checkpoint recovery plus actual
conditional outcome; WB rejection takes priority. WB emits separately qualified
saved-index training. Repeated RAS restoration must not reset gshare history.
Successful nonflushing vector configuration must not restore history: its
younger fetch stream remains live. In particular it cannot override a
simultaneous MEM branch correction. Only a configuration fault recovers history.
Start and context/code invalidation clear history, BTB, and RAS, but never
the direction tables. Their reset scrub is autonomous and must not gate fetch.

Run `bpred-tournament`, `bpred-btb-external`, `rv2wide-bht`, the existing assembly/frontend baseline fixtures,
and the production core/fetch fixtures for changes across this path. The BHT
fixture checks public replacement timing under blocked assembly, direction
overrides, compressed/straddled branches, replay, and history metadata. The core
bench checks saved-index training and recovery checkpoints against offered
instruction metadata.
The `rv2wide-direction-prefix` and `rv2wide-direction-prefix-rv32` fixtures
check direct prefix ownership with fixed RV64 and RV32 ports. They cover stale PC/length hints,
idle/replay bubbles, continuation faults, clear/capture collisions, immediate
target repair, distinct lookup/history checkpoints, sign boundaries, and XLEN
wrap. The compact variant also rejects out-of-range captured PCs and targets.

The frontend instantiates shared `Btb` with `fetch_bytes = 8` and external
directions when enabled. Its source-PC and target widths use the successful-PC
bound: out-of-range source queries miss, while targets retain speculative
normalization. Architectural fetch, recovery, and fault addresses remain exact.
The frontend also instantiates shared `Ras` and independently disableable
`TournamentBht`. S0 admits a PC into the registered S1 lookup. S1's combinational BTB/RAS result
selects the next S0 request directly and travels with its original lookup into S2.
Byte-six continuations retain their prefix prediction instead of looking up a
new branch. Replay retains that continuation context; the assembler also retains
the prefix prediction across straddles. A saved cursor holds an unaccepted
successor when array admission or block credits prevent a request.
Instruction metadata survives issue-window compaction and every pipeline stage.
Do not infer prediction ownership from a lane or an aligned block PC.

The cursor and S1 retain the hint (valid, target, length, and RAS action), not
its source PC. A valid ordinary BTB hit must be in the lookup's eight-byte
block. S2 keeps its three-bit byte offset; for a continuation the offset is
six and the source block is the preceding block, modulo the predictor width.
Reconstruct the source once for direction correction, invalidation, and
assembly. Never reconstruct the target relative to the block: the original
BTB/RAS destination survives backpressure and replay. The saved cursor, S1,
and S2 retain the requested byte PC in 32 bits on RV32 and 40 bits on RV64.
The extra RV64 bit distinguishes the first sequential block beyond positive
Sv39 from its negative canonical alias. Check live XLEN candidates before
narrowing; sign-extend stored PCs for lookup, translation, and fault reporting.
The extra bit does not widen admission: nonrepresentable architectural redirects
are rejected in the backend, and a sequential crossing cannot issue a physical
request even if translation returns an executable address. Keep fault values at XLEN.
Preserve all valid, length, RAS, direction-row, and replay fields and the existing
capture/clear priorities. Compaction must not add a stage or change block credits.
The `rv2wide-frontend-retention`, `superscalar-frontend-three-retention`,
`rv2wide-frontend-retention-h`, and `rv2wide-bht-retention` fixtures hold
continuations across BTB retraining, translation replay, and cache replay.
They check negative and wraparound PCs and targets under packet backpressure
and verify exact full-width redirect and continuation fault addresses.

Assembly validates predictions while expanding the existing two candidate
instructions. J/B immediate targets override matching taken BTB targets; target
mismatches invalidate the stale entry. Only JAL/C.J and RAS returns discover
fallbacks; never discover a conditional branch through the BTB's unconditional
discovery port or infer an unknown direction from its immediate.
Assembly also refreshes known return targets from the live RAS before the
accepted packet's stack action. Treat a changed RAS target as a cursor repair,
not stale BTB contents: retain the entry and its return hint. Straddles wait
for complete assembly, and stalls emit neither repair nor stack action.
Architectural restart/redirect, accepted assembly repair, fresh S2 direction correction, and S2 replay take
priority over S1 prediction and the saved cursor in the S0 request mux.
Architectural and assembly replacements may reuse storage that their clear
discards. Direction corrections preserve buffered blocks and their current S2
response: cancel younger reservations, but admit the replacement only when
registered free slots exceed that retained response's reservation. Replay
publishes no current response but still requires a free buffered-block slot.
Save a credit-blocked target in the cursor and issue it when capacity returns;
do not defer cancellation or history correction with target admission.
On accepted assembly repair, clear old S1/S2
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

At EX→MEM each lane captures one XLEN address word: the resolved target for a
branch or jump (even if not taken), otherwise the effective data address after
pointer masking. Select with decoded branch enable, not fault-qualified training
enable: legality gates the consumer, not the word captured. The live EX cache
lookup uses the data address before this selection. MEM and WB expose the stored
word to the owning consumer: resolution and BTB training for a branch, or memory
and prefetch for a data address.
The predicted target and the trap value remain independent.
MEM resolves the actual successor and misprediction and retains them through WB.
MEM correction preserves its own transfer and kills younger tokens. WB alone
qualifies training and resolved RAS actions from the successful retirement
prefix. Each MEM lane resolves independently, with the oldest correction
retaining priority. RR permits at most one actual or speculative RAS action
per group; conditional branches do not consume this restriction.

`branch-training.rhdl` owns the two-entry training FIFO, accepting all successful
WB branches in age order and emitting one BTB/tournament update each cycle, bypassing
the oldest incoming update when empty. A training record stores a canonical
source PC, the full resolved target, the successor step, one branch outcome,
and the independent direction lookup row and component predictions. Rebuild
the base index and parcel bits from the source PC when emitting the update.
Never truncate the resolved target to the source PC width.
After reset warm-up, both components train every conditional outcome; the chooser trains only saved
disagreements toward the correct component. Use current counters, not saved
counter values, so delayed aliased records compose. RR projects occupancy through the
WB, MEM, and EX branch groups, subtracting one training write at each edge
and saturating at zero. It admits a new prefix only if its projected WB update
fits; WB never waits for training capacity. Faults and flushes only reduce
the projected arrivals. Projection and capacity assertions enforce this booking.
Speculative recovery preserves retired training; predictor clear cancels
queued training, not previously learned direction counters. RAS resolution and
the group-width conditional history commit bypass this FIFO and reach the frontend
on the retirement edge. The frontend folds
history in lane order before command recovery, independently of counter writes.
Core tests compare emitted training against an ordered retirement oracle,
exercise sustained dual-branch pressure and oldest-stop prefixes, and verify
the one-RAS-action restriction. The BHT fixture checks a dual history commit
without a concurrent table update.

MEM flush restores through current WB, and the surviving corrected
branch restores again at WB with its actual action included. Pause assembly
speculation on this reconciliation edge because RAS restore wins speculation.
WB faults/replays restore without training rejected branches. Architectural
translation invalidation and FENCE.I clear target, RAS, and history state without
restarting direction-table warm-up.

A nonrepresentable MEM redirect stops fetch without forwarding its target.
Only successful WB branch retirement installs that target in the existing
precise-trap record, marked by one redirect flag, so the link write survives and older faults can squash
the branch. Invalid WB redirects, including xRET and trap vectors, use the same
slot. Classify the exception in the following cycle's architectural context;
wait for older accepted work to drain before issuing its CSR command. Trap PC
and value name the target, not the branch. A rejected trap-vector replacement
wins over release of the previous target. Frontend admission accepts only
representable redirects; sequential boundary faults use their compact encoding.

A pending fault squashes younger pipeline/window work immediately, but its
CSR trap command and public redirect wait for both completion credits and memory service to drain.
Same-group older acceptance participates in that drain decision. Branch and
replay redirects do not cancel or wait for successful accepted work.
Reset is coordinated with the memory service; it ends the entire response epoch.

The frontend pairs one accepted S0 virtual index with S1 physical permission
resolution and an S2 block outcome. Both lookup contexts use flushable Flow
Valid pipes. Reserve one of the assembler's three block slots for each admitted
lookup using occupancy plus same-cycle resident block releases. The assembler
owns three compacting block slots and a two-block decode window with live input
bypass; two C expanders feed the existing eight-entry instruction window without
another instruction queue. One consumed-halfword cursor retains unfinished bytes
in their original block, including incomplete 32-bit instructions. Decode the
second instruction across a sequential block boundary whenever bytes are available;
do not wait for it when the first instruction is already complete. Advance the
cursor and release zero/one/two blocks only with packet acceptance. Credit only
resident releases, not a consumed live input, and keep block admission independent
of packet readiness. Never depend on current
issue readiness to accept a cache result. Replay kills younger lookup stages only
and preserves buffered blocks and unfinished bytes; MEM/WB recovery clears assembly and
the instruction buffer. Start is
quiescent-only and the buffer is already empty. Accepted refills belong to L1I
and remain live across speculative cancellation. Fetch faults carry explicit
cause/address through the ordinary instruction token and suppress decode
hazards, memory requests, branch effects, and GPR writes.
Keep canonical decode bits separate from raw encoding and sequential PC. Link and
system recovery use the sequential PC; illegal trap values use the raw encoding.
A faulting continuation uses the instruction's original PC and the next
block's fault address. The CSR bank's C configuration must agree with this IALIGN.

The canonical `SuperscalarFetchBlock` keeps a compact PC and a normal/fault tagged
outcome. The normal valid/compact-target fields share storage with the architectural cause,
a positive sequential-boundary bit, and optional guest provenance. A failed fetch reports
its own PC as its fault VA; admission asserts that equality, so storage need not
duplicate it. Reconstruct the full fault before assembly selects an instruction's
owner: a straddle retains the continuation's VA, not the prefix's PC. Guest
provenance remains independent. The frontend and standalone assembler use the
same fixed RV32/Sv39 PC domain as the issue window. Normalize incoming blocks
once at the producer. A stall that captures the bypass must not change the
visible packet. Absent fault
payloads and invalid targets become zero, and a fault suppresses any provisional
BTB prediction. Store prediction ownership as one of four current-block parcels
or the preceding block's final parcel, not another XLEN-wide PC. Admission checks
that a valid non-fault prediction names an aligned halfword in that domain.
Expansion concatenates current-block PCs and decrements the block number for a
preceding prefix, preserving full-width stale-entry invalidation and XLEN wrap.
Retain one ten-bit gshare row for the four current-block parcels and a separate
prefix row. Reconstruct the low two index bits from parcel position, and the
bimodal/chooser index from that instruction's PC. Admission asserts these indices
for valid predictions; parcel zero supplies the main row even when its prediction
is invalid. Invalid indices are not an observable training contract. Never derive
a lookup row from the per-parcel recovery history: S1 captured the row before S2
walked the conditional branches. Capture one ten-bit history seed (the prefix's
pre-update history) and each position's valid, taken, base, and gshare flags.
On resident reads, start at the seed and append each valid outcome, prefix
first; each parcel observes the history **before** its own update. Live bypass
uses S2's already-computed histories without a second scan. Assert equality
at admission for all four parcels, including invalid predictions: nonbranch
recovery still uses their history. Only the compact block moves between slots;
the three credits, release rules, and ready path do not change.

The frontend also selects this PC width for speculative BTB/RAS destinations.
Direction correction and assembler direct/return fallbacks normalize targets
before both steering and metadata publication; stale-target comparisons use
the same representation. The assembler checks that incoming predictions already
obey it. Do not normalize architectural redirects, resolved branch targets, or
fault addresses. MEM compares the reconstructed hint with the exact next PC and
squashes younger aliases. Normal/fault unions remain sized by their larger
alternative, so narrowing a target alone need not shrink a queue entry.

`SuperscalarPipelineConfig.compressed` retains C as the mandatory base and accepts optional
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
The memory adapter owns exactly one request-context register between these
phases; do not duplicate it in a resolver or cache adapter, or make EX admission
depend on a TLB result. The translation services are unbuffered at the query
boundary. Their result carries a full physical address,
known/fault status, PBMT, guest provenance, execute-read intent, and probe
contention, not TLB leaves. Identity resolves immediately; paged translation
retains its own pending walks and exact failed lookups. Core, adapter, and
resolvers use shared `RiscvTranslationState` from `cores/common/mmu/context.rhdl`.
Both resolvers retain whole-state comparisons for prefetch cancellation;
preserve that policy independently of RV5Stage's selected-control comparison.

The adapter takes one `SuperscalarTranslationSpecialization`, an immutable
generator parameter implemented beside each resolver. Its `circuit()` builds
the configured implementation; `architectural_xlen()` determines every architectural
adapter port width, using the captured XLEN for identity or RV64 for Sv39.
`physical_map()` supplies the same map used by
the resolver's vector proofs and prefetch checks to the adapter's demand, split,
and PTE qualification. The specialization is the adapter's only generator argument;
keep XLEN and physical-map selection with the resolver.
`page_walk_clients()` declares zero or one PTE
client, and `split_access_policy()`/`atomic_pma_policy()` select explicit host
policies. Identity chooses ordinary split access and cacheable/readable atomics;
Sv39 preserves guest split context and requires ordinary/readable atomic memory.
The adapter consumes these capabilities and policies without a second mode flag
or module-name inspection. Keep circuit construction and these declarations
together in the specialization, not as independently supplied caller options.

`memory/transport.rhdl` has two physical clients for Bare (core, split) and
three for paged translation (core, split, PTE), in that priority order. Only
physical acceptance spends a credit. Local PTE denial may consume the walker's
offer with an admission fault but cannot spend a credit. The transport wraps
the opaque context with the selected client and removes that layer on response.
It has no invalidation input; cancellation changes translation publication, never
an accepted reply's destination. Returned context and Flow demultiplexing preserve
structural trace ancestry without a module summary contract.

The adapter retains native-word fragment size and Bare's existing atomic PMA
qualification. Paged translation retains HLVX execution checks and guest pointer
interpretation. Vector windows and certificates reuse the shared `cores/common/mmu/`
components in each resolver. Identity prefetch keeps one stage; Sv39 keeps two
and arbitrates its nonallocating probe below fast vector certification. These
are independent of the common transport's acceptance and response lifetimes.
Bare also retains its original data-cacheability restriction for instruction
hints; do not expand eligibility to instruction-only cached regions as part of
an ownership refactor.

At the coherent L1D boundary, place the native-word byte mask in the addressed
row without shifting the store value. L1D scalar mode does that shift and
returns the selected word in the low XLEN bits. The MMU, PTW, split engine,
and uncached transport do not use the SRAM row size for access alignment.
Run `superscalar-wide-cache-rv32` and `superscalar-wide-cache-rv64` for changes
to this conversion.
WB data demand wins DTLB contention over MEM, whose token receives Replay.
Only WB data misses enter the retained pending-walk slot. An instruction walk
already in progress finishes first, and the pending WB lookup wins next admission.
The walker captures its request context, independent of subsequent live CSRs.

Each port retains failed walks as `SuperscalarTranslationFailure`: the exact
lookup/context, fetch fault validity and cause, and guest provenance, not a complete TLB fill.
Replay matches the entire lookup, including access class and permission controls.
The lookup supplies the original access for cause selection; a matched failure
projects a zero physical address and zero leaves onto the common result path.
Successful completions invalidate that port's cached failure, while architectural
invalidation suppresses publication and clears both ports. Preserve these edges
and the independent instruction/data owners. Vector fallback probes cannot use
window/certificate bypass, and failed translations cannot certify a window;
physical vector retries supply their own address and PMA attributes.
The MMU fixture covers delayed simultaneous fault replay, exact address/access/
context matching, invalidation, stale NC mappings, and implicit guest-PTE provenance.

Optional `profile.svnapot` passes through the production top's Sv39 specialization
to the shared walker's structural PTE validation. Keep the shared mapping's page size
intact through fill and lookup; do not implement another NAPOT decoder or expand
a leaf into sixteen fills. ISA/UDB metadata follows the same profile selection.
The existing MMU fixture checks cold walks from different subpages, warm I/D
offsets, MEM/WB address agreement, mapping bounds, permission/A/D faults,
reserved encodings, and whole-mapping invalidation/remapping. Run `rv2wide-mmu`
and `riscv-svnapot` plus `tests/profile-test.rhm` for this integration.

Optional `profile.svpbmt` configures the shared CSR bank and ISA/UDB publication.
Carry the bank's PBMTE in shared `RiscvTranslationState`; shared CSR translation
flush already handles interpretation changes. Resolve architectural PBMT with
`riscv/rtl/svpbmt.rhdl` only after translation. MEM qualification, WB physical
requests, split-fragment permission checks, and prefetch probes must agree.
Implicit PTE traffic retains physical PMA semantics; management routing uses
physical cacheability to reach resident aliases even through NC/IO mappings.

The [common physical fetch router](../common/fetch/DEVELOPING.md) preserves the
S1/S2 lookup contract and retains one
nonallocating block until a retry consumes it. A changed retry may discard an
obsolete completed block. Architectural flush detaches it, not accepted bus
ownership. The cache adapter arbitrates data before fetch around the existing
uncached transport. IOMSHR admission remains independent of a younger fetch
owner, and data drain excludes that owner, preventing a WB/fetch dependency
cycle. The adapter withdraws unissued canceled fetches and drains issued ones.
Extend `rv2wide-mmu` for PBMT permissions/context and `rv2wide-fetch` for
real PBMTE CSR writes, NC/IO execution, shared data/fetch arbitration, and
redirect/FENCE.I behavior; run `tests/profile-test.rhm` for publication.
The existing `rv2wide-cache` fixture varies management-request PBMT at its
physical boundary while retaining real dirty cache contents and CHI snoops.
This checks that NC/IO aliases do not turn maintenance into local completion.

Optional `profile.svinval` selects the pure ISA catalog and composed system
columns, and publishes Svinval through the same ISA/UDB projection. SINVAL.VMA
reuses the shared CSR bank's `SfenceVma` action. Its address/ASID operands are
unused because invalidation is global. The two ordering instructions instead
use `TranslationOrder`, preserving their distinct TVM permission rule and
avoiding unnecessary TLB flushes. All three reuse system serialization and
successor restart; no new transaction state belongs in the core.
Extend `rv2wide-core` for M/S/U permissions, TVM, accepted-load drain, and
invalidation pulses; `rv2wide-fetch` rewrites warm I/D mappings through a
coherent PTE alias before executing the complete ordering/invalidation sequence.
Run those fixtures, `rv2wide-mmu` for canceled accepted-PTE response ownership,
and `tests/profile-test.rhm` for configuration/publication changes.

Architectural invalidation clears TLBs and retained faults at the retirement edge.
Registered walker cancellation breaks the retirement/readiness feedback path;
the invalidation edge suppresses completion publication and new walk admission.
Core/PTE physical requests use fixed core-first arbitration and carry client
identity in their context. Accepted context and outstanding credits survive
flush: they route each response even while the walker drains a canceled epoch. A PTE admission fault consumes
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
until the context-bearing atomic response transfers to completion arbitration.
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

Management uses the split access's retained WB owner and a management context
tag on its memory request. Capture squashes younger work while permitting an older peer to retire.
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
Hints select `CacheOperation.None` and suppress GPR writeback. Fork successful WB retirement
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

## Locality hint ownership

Zihintntl overlays the four exact ADD-to-x0 encodings in the composed decoder;
the encoded rs2 selects locality and is not a register operand. The shared
compressed expander supplies the same canonical encodings. Keep the architectural
`RiscvMemoryLocality` selector separate from the shared L1D's allocation policy.

WB folds only the successful retirement prefix into `wb_pending_locality`.
An ordinary instruction leaves Default; an NTL leaves its selector. The younger
memory lane uses its older peer's selector directly, while slot zero uses the
pending register. Do not derive this choice from request readiness or retirement
feedback: those depend on memory admission. CSR redirects clear pending state;
replay redirects preserve it. Split capture retains its own effective selector.

Carry request locality through MMU translation, split fragments, and physical
cache adaptation. Page-table traffic explicitly uses Default. The core fixture
checks pairing, replacement, consumption, replay, split ownership, and killed or
trapping targets; MMU checks translation and fragment metadata, cache checks
no-allocation misses, fetch checks compressed aliases, and FP checks hinted loads.

## System operations and precise boundaries

Optional Sstc is selected by `profile.sstc`, forwarded into `profile.csr_config`,
and published through ISA/UDB metadata. Reuse `RiscvCsrFile`'s timer state,
comparison, access gates, and pending-interrupt selection; do not add an Superscalar
comparator or timer-specific decode. The existing `time_counter` connection,
WB interrupt boundary, deferred-work drain, and WFI wake path own integration.
The core fixture drives time independently of cycles and checks full-width
deadlines, M/S/U access gates, STIP/reprogramming, WFI with both global-enable
states, handler rearming/return, live versus retired instruction pairs, and pending-load drain. Profile
tests check bank/publication agreement; the cosim pass fixture selects Sstc on
both integer and FP specializations without introducing another SoC test row.

Optional Zihintpause overlays the exact PAUSE cube on FENCE in the combined
decoder. Its independent `pause` control leaves CSR, fence, memory, and
writeback enables inactive. RR permits an older peer but stops younger issue
while PAUSE occupies EX/MEM/WB, without waiting for older deferred work to drain.
Successful WB retirement starts the 16-cycle RR cooldown; redirects, interrupts,
and reset cancel it. Do not reuse system serialization or the WRS retained owner.
The issue-window contents survive cooldown and completion arbitration continues.
Preserve the successful retirement-prefix qualification so killed hints cannot
start a cooldown. The core fixture checks both slots, consecutive hints, prior
loads, killed hints, interrupt entry, and reset; the fetch fixture covers real
instruction delivery and traced retirement. Keep neighboring FENCE decode intact.

RR serializes state-changing system instructions after older pipeline and accepted memory work
drain and never admits a system instruction as the younger slot, except for
vector configuration's non-draining path described below. Pure CSR reads also
issue alone, without the global drain. Use the shared `csr_pure_read` and
`csr_read_wait` policy, with older scalar/vector FP and vector progress summaries
including EX/MEM/WB producers. CSR results are unavailable until WB, where the
normal forwarding path selects the shared bank's legal result.
Younger ordinary instructions may issue, but a serializing WB system command
flushes their effects and refetches from the architectural successor or trap/return PC.
This cut gives WB CSR recovery priority over a younger MEM branch without
making feed-forward stage registers elastic.

Command selection precedes side-effect-free `command_fault` evaluation. Fold
denied reads into the existing fault-retention path without selecting their own
candidate through the resulting fault. Successful reads do not squash, resume,
or restore prediction history. Preserve the older Vset/younger-fault command
priority and keep pure reads out of two-instruction groups.

FENCE and FENCE.I share this drain/serialization path, with no CSR action or
writeback. Successful WB FENCE.I drives a separate `instruction_invalidate`
Pulse alongside the successor redirect. The frontend forwards it to the shared
L1I invalidate input; L1I owns suppressing installation by old retained refills.
Do not emulate instruction invalidation with speculative flush alone.

Optional Zawrs adds a wait selector to the composed system decode and reuses
RR system serialization. WB retains the WRS owner in the same slot used by split
accesses and cache management, flushes younger work, and suppresses memory
dispatch for that owner. No elastic pipeline register or deferred load owner is
introduced. `SuperscalarMemory.reservation_valid` is live shared-L1D state routed
through the cache adapter and MMU, not a second reservation tracker.

The saturating 12-bit WB counter implements the STO bound and the below-M TW
bound for NTO. Reservation loss and `RiscvCsrFile.wfi_wake` take priority over
timeout; successful completion retires once and redirects to the successor.
A TW timeout uses the normal retained fault path with the original instruction
PC and raw encoding. Interrupt entry cannot abandon the pending wait: wakeup
first retires WRS, then normal interrupt handling uses its successor. Keep these
rules aligned with RV5Stage without importing its pipeline types.

The existing core fixture covers drain, privilege/TW behavior, timeout-edge
wakeup, interrupt boundaries, reset, and killed waits. The cache fixture checks
CHI-snoop wakeup; the fetch fixture checks real LR/STO/SC reservation retention
and event-trace ownership. Profile and cosim-pass tests cover publication and
instrumented elaboration. No WRS-specific SoC or CI lane is needed.

The pending-trap register holds the boundary instruction identity, an interrupt
flag, and the resolved synchronous cause/value independently of the speculative
pipes. Only H profiles carry guest fault details. Do not retain a CSR operation,
source operand, or branch execution record: a held exception enters the CSR bank
as an explicit fault, never as a reissued instruction. The passive observation
projection preserves the faulting instruction's next PC, fetch, memory, FP, and
vector provenance without retaining those fields in an unobserved netlist.
The separate service record still owns delayed operations that can retire.
An older accepted load paired with a younger fault
retires once, preserves its completion owner, and delays trap entry until the
completion credits, service, and unflushable completion pipe are all empty. Interrupts
inhibit new WB memory acceptance before retaining the oldest unretired PC.
If an interrupt disappears while draining, recover to that PC without trap entry.
An empty-pipeline boundary uses the last retired successor (including branch
targets), never an arbitrary fetch cursor.

Only system/fault commands enter `RiscvCsrFile.commit`; ordinary instructions
feed the explicit two-bit retirement count. Superscalar may count an older successful
slot on the same edge as a younger synchronous trap. RV5Stage continues to supply
zero/one. Keep CSR write priority and pre-transition privilege filtering in the
shared counters; completion is not retirement. Architectural CSR descriptors and
trap selection are reused, not copied into named-core control logic.

## FP scheduling and ownership

`fp.rhdl` owns the FPR file and the shared execution service. The core selects
`SuperscalarFp` or the stateless `SuperscalarFpDisabled` circuit at elaboration time;
the enabled implementation has no disabled-mode branches. RR books its
fixed-result calendar one cycle before EX and captures the selected slot's
three operands in the EX payload. The booking owns the execution input even
when a flush suppresses its request, so variable-request readiness cannot feed
back through same-cycle recovery. WB authorizes the resolved oldest successful
prefix independently of CSR command-success feedback. Authorization travels
for `latency - 2` cycles and qualifies the corresponding fixed numerical return.

The optional `SuperscalarPipelineConfig.half_precision` and `zfa` select shared `fp_instructions`,
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

Pass the same floored `fp_timing` to the vector backend with `fixed_fp` enabled.
Vector launch atomically books the shared return edge and the backend's VRF
calendar. Route tagged results directly from the scheduled execution service
to the backend, without completion storage or a Valid conversion. Fixed results
must write on their booked edge; divide/sqrt retains its result at the numeric
producer under backpressure and cannot consume a fixed reservation.

`SuperscalarFp` receives complete FP controls for RR probe/booking so its second
calendar reserves only actual fixed FPR writers. Vector-to-FPR moves use the
backend's `floating_writeback` claim at sequencing, before synchronous reads.
RR probes registered occupancy and books ahead of same-edge vector claims,
matching GPR calendar ordering without observing its own live booking.
Neither can displace an already issued fixed write. Scalar variable FPR returns
remain backpressured at the numerical producer on a reserved cycle. The service's existing aging
pause also stops new FPR claims, allowing that variable result to drain.
Scalar arithmetic and vector moves share FPR port one without a result holder;
port zero independently serves immediate hits ahead of delayed loads.

`rv2wide-fp` checks two-cycle vector returns, consecutive launches, ordered
recurrence spacing, and a blocked divide preempted by a scheduled fixed return.
It also checks direct FPR forwarding beside a load hit, delayed-load backpressure,
scalar/vector reservation collisions, variable divide retention at a booked FPR
edge, and same-address vector-owner replacement on the older owner's write.
Its scalar tags exercise both 39-bit PC limits, XLEN wrap, live-input replacement,
and rejection of a nonrepresentable admitted PC. RV64 core/FP fixtures also
use fixed 39-bit storage and compare reconstructed completion identities; the core
fixture covers delayed loads and both integer M services at the PC limits.
`rv2wide-core-vector` uses 3/5/2-cycle FP timing to exercise matching core/backend
reservations with shared scalar/vector execution and half-precision arithmetic.
It also checks that a pending vector-to-FPR move allows independent vector and
scalar FP work to advance while its true FPR consumer remains interlocked.
`rv2wide-core-fp` exercises 3/5/2-cycle execution; `rv2wide-core-fp-late`
uses 3/5/3 cycles to collide integer-returning FP operations with multiply returns.
Both enable Zfa; the first selects Zfh and the late fixture selects Zfhmin.
They share a public-interface bench covering both age slots, FPR dependencies,
loads/stores, killed EX arithmetic, divide, flags, and illegal FS/rm. Alternating
FP owners across paired and single issues, bubbles, and a fused operation check
the group-owned operand snapshots. An older taken branch discards its younger
FP partner at MEM; a faulting older peer suppresses a younger native-width
divide before a subsequent group replaces its shared request.
Native sqrt and RTZ/dynamic-RUP divide cases check the compact retirement
record's operation, precision, rounding, and NX contribution in both XLENs.
Branch/divide pairs compare integer and FP inputs with opposite orderings,
checking both issue positions and cancellation of the younger divide.
The core-vector busy-divider case reverses operands on the replaying divide
and checks the raw FP result after reissue.
Zfa cases cover both precisions, static/dynamic rounding, NaNs, modulo conversion,
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

## Vector integration

The vector wrapper enables the shared engine's packed multiplier. Keep its
subword issue, VRF return reservations, and SIMD completion ownership inside
`cores/common/vector/`; the named core continues to own only shared full-width
multiply/divide scheduling and scalar pipeline policy. Do not route packed
VRF results through the scalar GPR completion calendar.

Keep architectural admission, retirement, and scalar hazards in `core.rhdl`;
`vector.rhdl` connects `cores/common/vector/backend.rhdl` to the local LSU rather
than reauthoring admission/execution composition or importing RV5Stage policy.
WB admission consumes registered descriptor credit, never downstream LSU
readiness. Memory admission checks retained scalar load owners and a sampled
quiet memory boundary: live drain signals can depend on current admission.
`profile.vector_dispatch_depth` selects descriptor capacity (six by default),
independently of completion slots and the single active sequencer. Keep the
shared admission queue's `~pipe: #false` setting when varying capacity; at full
occupancy its credit returns on the cycle after a dequeue.
Six resident descriptors cover the nominal six-cycle WB replay/refetch/reissue
round trip while dispatch drains one descriptor per cycle. This is not a bound
on frontend cache misses, operand hazards, or other recovery delays.
Faultable descriptors retain their WB owner until certification or precise
execution produces an outcome. Eligible younger scalar instructions may execute
but a live WB offer behind unresolved authorization replays its oldest PC once.
After that rejection, a registered RR interlock parks the refetched suffix until
the vector outcome; do not poll authorization by repeated pipeline/refetch trips.
The initial speculative pass remains enabled and no scalar result is buffered.
Keep the accepted vector owner, element checkpoints, and responses across that replay.
Gate services and architectural effects through the existing retained-owner
dispatch/retirement authority, before request formation. A younger fault is
replayed rather than retained ahead of the older unresolved vector operation.
The acceptance edge applies the same rule to a younger peer in the WB group.

Successful vector resolution does not redirect fetch. Its retirement may share
the second retirement slot with one ordinary scalar ALU/branch result, using
the registered GPR calendar to prove that port free. Do not query a live FP
completion to make this selection: completion depends on WB authorization.
Service/CSR work and excess live suffixes replay into their original WB lanes;
no completed-result storage or third retirement slot is introduced. The second
retirement owner's explicit Flow selection preserves original MEM ancestry.
Restore predictor state at the rejected scalar PC, not on vector capture, and
give an older vector fault priority over scalar replay and MEM branch repair.
Traps and direct CSR accesses still drain accepted work; vector configuration
and vector admission retain their uncertified-memory interlocks.

The shared engine pipelines aligned faultable elements behind its ordered
authorization frontier. `vector-memory.rhdl` resolves one attempt per cycle;
retry/fault flushes younger unaccepted stages without erasing accepted response
owners. Misaligned elements stop younger sequencing before split takeover and
retain that barrier through their own feedback. Older aligned decisions do not
release it. Keep store mutation at WB resolution, never speculative lookup.
Packed loads launch lookup directly from their prepared issue token; stores and
elementwise accesses retain the ordinary EX register. An older registered lookup
has priority, with a blocked certified attempt retained by `VectorReplay` until
transfer. Packed hit data returns two cycles after issue, one cycle earlier than
ordinary memory; the shared engine reserves the corresponding VRF write edge
before sequencing.

EX forms a conservative single-page range from the forwarded vector state,
scalar base/stride, and vstart using shared vector footprint geometry. Its
`vector_fast_eligible` flag qualifies only that proposed range; the descriptor's
`precertified` remains false until MEM captures a successful `VectorFastCheck`.
Only `certificate.range` is meaningful before that capture. MEM uses the DTLB's
independent probe with demand-accurate permissions/A/D/PBMT. WB demand, fallback
page proof, and ordinary lookups keep the demand port; certification replaces
only an optional data-prefetch probe. Suppress that hint rather than forwarding
metadata from the vector lookup. Failure never allocates a walk or produces a
speculative fault.
Bare memory supplies the same contract using identity mappings and page PMAs.

WB revalidates context, invalidation epoch, and pointer masking, then retires an
accepted certified memory descriptor normally. Only uncertified memory enters
the retained WB owner and its younger scalar WB authorization barrier. Keep
the shared admission controller's existing precertified bypass of page proof
and execution quiescence; do not relax the precise fallback or scalar-memory
ordering gates.
The accepted descriptor carries the PPN/range into vector execution and local
replay. Forward the adapter's physical-address flags through the selected memory
owner: MMU lookup and WB demand must not translate these physical retries again
or depend on the original TLB entry or fallback page window remaining resident.

`vsetvli`, `vsetivli`, and `vsetvl` consume the CSR command but not a WB service
admission. They may pair whenever the common dependency/resource/order checks
permit, without draining vector execution. Keep the single-configuration limit;
there is no same-pair VL-to-GPR or EX ALU-to-configuration bypass. Early constants
can supply configuration GPR inputs through ordinary RR capture. Fixed FP WB
authorization permits an EX-validated configuration on the same edge, without
depending on CSR command-success feedback; other system commands serialize.
The existing two GPR ports and lane-qualified CSR result selection need no
additional queue or stage. Cores built without vectors read committed CSR state
directly: they have no MEM/WB configuration snapshots or same-group bypass.
Vector-capable builds keep forwarding regardless of runtime vector enable;
legality still controls whether a configuration can claim it.
EX computes configuration and its GPR result using shared architectural helpers.
The newest nonfaulted configuration in MEM, then WB, precedes committed CSR
state. Within an EX group, each lane uses the youngest older configuration, if
any. Legality, descriptor snapshots, and fast memory geometry all use that
forwarded state, never RR's potentially stale committed vtype. Ordinary operand
forwarding carries the new VL to dependent scalar work and configurations.

WB selects the configuration's actual lane for CSR commands, GPR data, and
write enables, without qualifying vector requests with final retirement feedback.
EX-validated configurations must succeed at WB. A younger fault uses the existing
fault-retention register for one extra cycle when an older configuration needs
the single CSR command port first. A trap behind an accepted older vector must
also observe that cycle's admission, then drain it before entering the handler.
Replays and retained vector memory discard
only the unsuccessful suffix: an older configuration commits once, while a
rejected older instruction or retained vector suppresses its younger configuration. Successful
configuration does not squash or refetch successors. Faults, interrupts, and
branch recovery still suppress wrong-path configuration and descriptor admission.
Direct CSR writes, traps, and translation/context changes retain full drain.

`memory/ports.rhdl` binds the [shared scalar/vector arbiter](../common/memory-arbiter.rhdl)
to the native pipeline ports and retains the split client through its final response. Ordinary responses route from the returned scalar/vector context;
a sixteen-entry credit counter preserves the former routing FIFO's capacity.
Vector slot/offset/width metadata travels through memory as well, with a slot-sized
credit counter replacing its response-owner queue. All accepted context and
credits survive scalar recovery. The MMU prioritizes WB demand over page-window probes and
speculative lookups, while keeping accepted PTW response ownership unchanged.
Translation-context changes must wait until the vector page window is released.

`vector-memory.rhdl` keeps strict `idle`/`requests_idle` separate from
`memory_start_ready`, the promise that a first faultable read can issue next
cycle. It credits an actual final certified release only when no older lookup,
MEM stage, incoming issue, or split remains. Already accepted response owners
still participate in full drain, but not in that launch permission. Keep this
permission at the first sequencing transfer rather than descriptor dispatch.

Scalar multiply and fixed FP reserve their EX slots at RR; squashing an EX
request must not combinationally change vector shared-service admission.
In scalar-only builds, the first register holds the selected half's subtotals
and its incoming carry. The second holds the XLEN result. WB applies MULW
sign extension and attaches its authorized owner for the final register.
Vector builds register the full double-width sum because full-width vector
elements consume both halves. Both paths publish scalar results at WB+1.
The GPR calendar includes ordinary last-slot writes as well as deferred
returns; a vector scalar-result reservation must not overwrite a slot already
promised to a scalar instruction. A waiting vector return pauses new bookings
long enough to create a free slot, while aged scalar completions retain bounded
priority. Vector-to-FPR ownership ends at its scheduled arithmetic-port write.
That ownership gates only another FPR-result owner, not unrelated vector work
or scalar FP execution. Preserve per-FPR RAW/WAW checks across pre-WB producers
and admitted work. Same-edge replacement wins the older owner's reservation
clear, including when the destination register is unchanged.

The single vector-to-GPR owner is capacity for a nonzero scalar destination,
not a vector dispatch barrier. RR and WB check that claim only for another
integer scalar-result writer; keep FPR ownership independent. The actual
nonzero return must match the retained owner's decoded destination and may
release capacity for same-edge acceptance. That cycle's completion uses the old
owner, while allocation wins the next-state update. x0 results neither claim
nor release ownership. Preserve the per-GPR scoreboard and calendar, and keep
accepted ownership live across scalar recovery until its scheduled write.

## Validation

Run `superscalar-memory-ports` for scalar/vector contention, MEM and WB
ownership, fault routing, and credit exhaustion under response backpressure.
For memory-adapter changes, run `rv2wide-bare-memory` and `rv2wide-mmu` first.
The Bare fixture checks exact EX/MEM timing, rejected physical offers, admission
faults, backpressured replies surviving invalidation, split routing, and vector
certificate epochs. The Sv39 fixture covers demand/walk arbitration, translation
and physical fault provenance, invalidation, PBMT, and vector-window contention.
Use `tests/xlen-test.rhm`, `rv2wide-rv32`, both wide-cache fixtures, and the
production fetch/cache/vector fixtures when moving shared access or owner paths.
The XLEN host suite also checks each specialization's capability/policy contract
and stable generator equality across width, physical maps, Svnapot, and hypervisor settings.

Validate the real shared engine, adapters, and integrated core with:

```sh
python3 tools/testing/rsim/run.py --fixture rv2wide-vector-memory --fixture rv2wide-vector-memory-rv32 --fixture rv2wide-vector
python3 tools/testing/rsim/run.py --fixture rv2wide-core-vector --fixture rv2wide-core-vector-rv32 --fixture rv2wide-mmu
```

The LSU checks cover raw-word extraction, rejected requests and store commits,
accepted-response backpressure, split fault addresses and guest provenance,
certified local replay, and store acknowledgements in RV64/four-slot and
RV32/one-slot configurations. The engine fixture exercises dependent SIMD/store
handoff, page-proof success/fallback, packed versus elementwise accesses, and
delayed certified loads feeding scalar extraction. These are service tests, not
evidence of vector instruction execution by `SuperscalarCore`. The core-vector
fixtures additionally exercise real decode/vset/CSR state, shared arithmetic,
packed and precise memory, two-port scalar writeback, and fault/vstart reporting.
Their vset streams check all configuration forms, MEM/WB and GPR forwarding,
consecutive issue without refetch, forwarded-vill legality, illegal keep-VL
transitions, configuration overlap with delayed vector loads, immutable old/new
memory geometry, and wrong-path configuration suppression after a MEM branch.
Paired streams check both lane orders, slot-one MEM/WB state forwarding,
same-pair legality and memory geometry, scalar RAW/WAW splits, precise younger
traps, replayed suffixes under injected rejection and full descriptor credit,
and retained older vector memory.
Authorization regressions let younger scalar work reach MEM before the vector
owner retires, bound WB retry independently of authorization delay without
duplicate accepted stores, and sweep one/two-slot arrival timing across final
authorization. They require an
actual vector/scalar co-retirement, suppress younger GPR/FPR/multiply/store and
fault effects, preserve the vector fault's `vstart`, and defer interrupts until
the vector owner and accepted responses have drained.
Vector-memory/branch pairs additionally require same-cycle MEM arrival for
loads and stores, both outcomes and prediction directions, certified retirement,
precise authorization replay, older rejection, and older vector faults.
Conflict-based pairing checks both age orders of vector/ALU, no-op, branch,
jump, and fixed-FP partners and configuration/memory/M/FP partners. It retains
service-admission, descriptor, CSR-command, FPR-read, and FPR RAW/WAW splits,
and checks configuration/FP authorization across fault/replay.
Ordered vector/store cases require same-cycle MEM arrival and hit/slow
co-retirement, store-commit and slow-request rejection, either owner's
fault/replay, and real full-VDQ rejection behind dependent divides. Their
public-port oracle checks that no store is offered before its older descriptor
retires and that replay duplicates neither descriptor acceptance nor mutation.
The same fixtures check load/M/variable-FP pairs in both age orders, rejection
by busy integer/FP dividers, and a genuine vector-scalar-result/store-data RAW.
Both core-vector fixtures run the event pass and production collector. Public
MEM observations, accepted descriptor requests, and sequencer dispatch/release
ports form an independent occurrence FIFO. It checks exact MEM parents for every
sequence transfer and stall across both lanes, queued dispatch, and replacement;
the existing replay/fault streams retain their architectural checks.
Scalar/configuration streams additionally check all configuration forms, normal
two-port writes, same-destination coalescing with distinct retirement values,
dependent AVL/VL splits, single-configuration bandwidth, and precise scalar faults/replays.
Scalar/vector arithmetic streams check same-cycle MEM/retirement, add/sub and
vector multiply/divide consumers, LUI/AUIPC and XLEN-width values, x0, a producer
waiting on a delayed load, late multiply interlocks, and either lane's fault/replay.
Independent pairs cover both orders, scalar-source WAR snapshots, VRF/immediate
versus GPR fields, late GPR-result RAW/WAW splits, accepted-vector drain before
a younger trap, and actual full-descriptor-queue replay without memory traffic.
Vector-to-GPR overlap cases hold a move behind a long-running vector divide,
require younger arithmetic/scalar-to-vector/memory descriptors and independent
scalars to retire before its return, and verify exact completion identities in
either WB lane.
An older x0 extraction must not claim or release the real owner. A second GPR
writer retains the one-owner interlock; arrival-offset sweeps check replay and
same-edge owner replacement without duplicate completions. Scan-to-GPR cases
cover population count and first-set index, including a zero-valued result.
They also check certified loads/stores retiring at ordinary WB without recovery,
younger scalar progress, physical remapping, stale-certificate fallback, and
cross-page fallback. MMU checks cover nonallocating cold certification, warm
host/guest mappings, current permissions/PBMT, simultaneous demand/certification,
context/epoch revalidation, and self-contained physical retries after TLB invalidation.
The RV64 fixture combines H with V/FP, checking both host/guest FS/VS gates,
guest-fault CSR provenance, and draining accepted vector loads before trap entry.
The MMU fixture certifies and reuses a nonidentity G-stage vector page mapping.
Certified-to-faultable overlap cases require tail-edge descriptor replacement
and a three-cycle hit-path tail-to-first-read gap for a cross-page strided load.
They inject an older cache replay, retain a delayed accepted response, and fault
the younger access while checking both older loaded words survive. Public
dispatch/sequence occurrences and memory effects provide the oracle in both XLENs.
Run the scalar core and late-FP fixtures when changing the shared service
reservations; retain their existing EX-to-WB timing checks.

`rv2wide-rv32` executes the production fetching/cache top in Bare mode. Its
public-retirement and CHI oracle covers RV32 arithmetic and high products,
division, compressed jumps, word-sized cached and IO beats, split accesses,
atomics, CSR WARL behavior, and precise illegal/access traps. Run it alongside
the existing RV64 core/fetch fixtures for XLEN or memory-adapter changes:

```sh
FIXTURES='rv2wide-rv32 rv2wide-fetch' python3 tools/testing/rsim/run.py
python3 tools/testing/rsim/run.py --fixture rv2wide-core
tools/run-racket-tests.sh cores/superscalar/tests/profile-test.rhm cores/superscalar/tests/xlen-test.rhm
```

For tracing changes run the batched `event-window` behavioral fixture and the
Mini/Simple trace smoke with the native Perfetto importer. Use `COSIM=1` on one
shape to check pass composition; ordinary core behavior is covered by the existing
`rv2wide-core` fixture. The trace smoke reuses the existing scalar cosim payload,
without adding software or CI config rows:

```sh
python3 tools/testing/rsim/run.py --fixture event-window
SOC=mini-rv2wide-rv64imacb COSIM=1 TRACE_FILE=/tmp/rv2wide-mini.pftrace TRACE_PROCESSOR=/path/to/trace_processor_shell moon run rhodium:sim-trace-smoke
SOC=simple-rv2wide-rv64imacb TRACE_FILE=/tmp/rv2wide-simple.pftrace TRACE_PROCESSOR=/path/to/trace_processor_shell moon run rhodium:sim-trace-smoke
```

`core.rhdl` declares the passive `rv2wide` adapter contract. Numerical observations
carry explicit FLEN; FPR effect masks must not be inferred from XLEN.
`rv2wide-core-fp-rv32` and `rv2wide-core-fp-late-rv32` run the common FP
hazard/ownership fixture at native single precision, including killed EX
results, paired GPR writes, delayed loads, split accesses, and WB divide.
The observation contract's typed tokens
use grouped branch state and compact FP scheduling tags; the native callbacks
retain their existing architectural fields and timing. The core declares
WB slots, split capture, CSR commands, and accepted service returns through
`cores/common/cosim-source.rhm`; `superscalar.rhdl` binds the sibling MMU's physical
provenance. Ordinary elaboration adds no observation ports, state, or DPI.
The simulation-owned [adapter](../../sims/cosim/DEVELOPING.md) assigns age IDs,
retains deferred owners, and resolves both slots at a settled sample barrier.
Vector capture and physical provenance remain simulation-owned. The shared
vector observer resolves execution effects after the hart adapter allocates
WB identities; retained memory instructions keep that identity through faults.
Keep replay and speculative EX multiply launch out of architectural admission.
The completion adapter follows the three unflushable RR-to-WB stages for load
and divide, but matches multiply directly to its authorized WB+1 RF-write edge.
It permits concurrent multiply writeback and variable-return admission. Update
that contract and its ownership tests if these latencies change.

For observation changes, run the native adapter and optional-pass tests, then
the existing software path on both shapes:

```sh
moon run rhodium:native-check--cosim-hooks-test
tools/run-racket-tests.sh sims/cosim/tests/pass/pass-test.rhm
SOC=mini-rv2wide-rv64imacb COSIM=1 moon run rhodium:sim-boot-test rhodium:sim-isa-smoke-test rhodium:sim-cosim-smoke
SOC=simple-rv2wide-rv64imacb COSIM=1 moon run rhodium:sim-boot-test rhodium:sim-isa-smoke-test rhodium:sim-cosim-smoke
```

Run the focused production-core fixture:

```sh
python3 tools/testing/rsim/run.py --fixture rv2wide-core --fixture rv2wide-core-fp --fixture rv2wide-core-fp-late
python3 tools/testing/rsim/run.py --fixture rv2wide-issue-window --fixture rv2wide-issue-window-h --fixture rv2wide-issue-window-rv32
python3 tools/testing/rsim/run.py --fixture rv2wide-cache --fixture rv3wide-fetch
python3 tools/testing/rsim/run.py --fixture rv2wide-assembly-prediction --fixture rv2wide-frontend-prediction --fixture rv2wide-bht
python3 tools/testing/rsim/run.py --fixture rv2wide-assembly-prediction-h
python3 tools/testing/rsim/run.py --fixture superscalar-issue-window-three --fixture superscalar-issue-window-three-h --fixture superscalar-assembly-three --fixture superscalar-frontend-three
python3 tools/testing/rsim/run.py --fixture superscalar-read-two --fixture superscalar-read-three --fixture superscalar-read-three-rv32
python3 tools/testing/rsim/run.py --fixture rv2wide-issue --fixture rv3wide-issue --fixture rv3wide-issue-rv32
python3 tools/testing/rsim/run.py --fixture rv2wide-mmu
python3 tools/testing/rsim/run.py --fixture rv2wide-fetch
moon run rhodium:check-boundaries
```

The rsim fixtures belong to `cores-components`. Their C++ scoreboards sample
pre-edge outputs, tick the model, then publish registered host responses. The
shared `tests/rsim/driver.hpp` makes those phases explicit and preserves
old-response sampling; falling-edge drivers prepare the next cycle. The
enabled-fetch fixture compiles with event tracing and links the production RHEG
collector directly. Its independent instruction model and trace oracle check both
retirement lanes after their clocked callbacks complete.

The read-path fixtures use the real 4R2W register file and the same forwarding
function as RR. They exhaust candidate-valid/source-use/x0 combinations, check
same-register sharing and inactive matching fields, both preferred and fallback
borrowers and donor priority, and exercise each of nine producer
positions with available, unavailable, and replaying results. An independent
youngest-first oracle checks simultaneous matching producers, randomized pending
scoreboards and writes, pre-edge RF reads, and reset retention. These checks
cover two-candidate RV64 and three-candidate RV32/RV64; they do not model
write-port allocation or treat read availability as an issue grant. When changing
core wiring, also run `rv2wide-core`, `rv2wide-rv32`, `rv2wide-core-fp-late`, and
`rv2wide-core-vector` for scalar, delayed FP, and vector-GPR return integration.

The issue fixtures compose the production decoder, issue module, and separate
register-read datapath. An independent C++ oracle exhausts ordinary source-use/destination
combinations and last-lane return reservations, then randomizes presence, physical
readiness and external blockers. Integrated core tests cover three independent
writes, destination aliases, and shortened retirement prefixes. Directed groups cover first-pair
rewrites, third-lane RAWs, deferred WAWs, scalar/FP/vector service collisions,
serialization, fetch faults, PAUSE, ordered memory, PC wrap and branch-training
capacity. Third-lane address tests cover both blocked and unavailable address
read paths, producer readiness, nonzero offsets, unrelated store data, and
nearest-writer priority. Integrated service tests check address forwarding and
same-destination load priority on hits, deferred returns, rejected admission,
fault/replay, older faults, retained split success/fault, and subsequent RF consumers.
They also require two ordinary writers to complete alongside load, multiply,
late FP, and vector scalar returns on the three-port specialization.
Operand checks cover every selected physical path, first-pair constants,
PC/register bases, signed immediate sums, later-stage mux controls, and blocked
sources/donors. They also check emitted instruction bits, decoded controls,
and source selectors independently of register data. Pair these fixtures with the four core integration fixtures above
when changing admission or WB routing.

The issue-window fixtures check two-entry RV32 and two/three-entry compact RV64 with and without H.
They fill all eight entries, retain mixed normal/fault owners under backpressure,
consume every supported prefix length while appending, and clear live offers. An independent
queue checks raw/canonical encodings, exact PC/successor and fault addresses,
guest provenance, sign-extended predicted targets, and direction/RAS checkpoints.
Distinct saved rows and histories check PC-derived training indices through
compaction and wrap, including nonzero histories without a predicted target.
Invalid valid-index/PC combinations fail admission; invalid training-index bits
are ignored. The core's retirement/training and recovery oracles check the same
metadata through both lanes and retained service/replay paths.
Directed values exercise sequential-boundary faults, sign-extended PCs, physical
upper bits, continuation faults, and XLEN wrap; invalid compact successful PCs
must trigger the ingress assertion.

The core fixture checks sustained dual
retirement, packet coalescing, age-ordered same-destination pairing, RAW/WAW and x0, youngest-producer forwarding,
RV64/word ALU operations, seeded dependency-heavy arithmetic, signed/unsigned
branches and jumps in either slot, JALR masking, misalignment/illegal faults,
MEM qualification, replay/restart, older-fault priority, and reset cancellation.
AUIPC/ADDI cases check signed-immediate extremes, carry/borrow and RV64 wrap,
same/different/x0 destinations, sustained pairing, EX and stored-RF consumers,
pending destinations, excluded neighboring operations, recovery, and reset.
Constant forwarding adds LUI/ADDIW signed boundaries and replacement of both
binary sources. Address composition covers both offset signs, 13/33-bit carry,
original register/PC bases, hit/slow services, simultaneous store-data dependencies,
split ownership, precise older/younger fault and replay, and excluded word/general
adder producers. RV32 fetching and FP/vector fixtures check their operand snapshots.
Same-pair ALU/conditional-branch scenarios cover every predicate, either/both
replaced sources, signed/unsigned disagreement, both prediction directions,
selected word/B/AUIPC results, sustained dual issue, producer RAW/WAW and unrelated
source waits, x0, excluded producers/JALR, older/younger fault and replay, and reset.
M scenarios exercise every RV64M encoding and result projection, signed/mixed/unsigned
high products, zero-divisor and overflow rules, overlapping pipelined multiplies
and loads, cross-service completion ownership, RAW/WAW interlocks, x0, and rejected
versus accepted operations across branch/trap recovery. A dependent multiply
consumer must reach MEM within four cycles of its producer's MEM token; repeated
consumers cover forwarding and same-register WAW release at the direct write
edge. Every accepted multiply completion must arrive exactly one cycle
after WB authorization (three after EX), including competing variable returns.
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
same-pair integer store-data forwarding across widths, lanes, hit/slow/split
service, sustained dual issue, producer interlocks, and rejected/killed stores,
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
It also holds retained loads across changing live inputs at 4 KiB, high-address,
and XLEN-wrap boundaries, checking exact successor, prediction metadata, one
request/retirement, and a subsequent GPR consumer.

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
The RV2Wide and RV3Wide fetch instances reuse that entire oracle.
The RV2Wide fixture additionally selects Zcb/Zcmop and executes every form,
all compact integer registers, dependent consumers, byte/halfword memory lanes,
and wrong-path store suppression. Both variants check original 16-bit trap
values after older load drain: reserved neighbors with the subsets selected,
and unselected optional encodings with C only. Profile tests check independent
selection, shared CSR configuration, publication, and invalid combinations.
The RV2Wide instance additionally requires warm conditional direction predictions,
including a 32-bit branch crossing an eight-byte fetch boundary. It also runs
compiler event instrumentation so accepted-prefix/local-repair ordering and
instruction ancestry are checked by RTL assertions. The
core fixture supplies explicit predictions to check correct taken branch/target
pairing and wrong-direction/target recovery; shared eight-byte cursor ordering
is covered by `bpred-btb-wide`.
`rv2wide-assembly-prediction` isolates stale entry boundary/length repairs,
immediate-target corrections without changing conditional direction, live-RAS
refreshes of warm returns (including compressed, straddling, and coroutine
forms), empty-stack target preservation, fallthrough
suffix cuts, predicted straddles and continuation faults, and
single-shot RAS actions under packet backpressure. Its D-enabled configuration
also checks all four compressed FP load/store expansions and raw encodings.
Cross-block cases cover every halfword start and instruction-length pair, stalled
packet retention, source-block history, immediate credit reuse, complete-first
progress without lookahead, and a second-slot continuation fault.
Relative prediction ownership checks every parcel at high/negative addresses,
exact stale-entry invalidation, continuation-only predictions crossing pages and
XLEN wrap, and rejection of unaligned or unrelated-block prediction owners.
The primary and `rv2wide-assembly-prediction-h` variants select compact 39-bit
PCs, with the latter enabling H. Both variants fill three
slots with independent faults or mixed normal/fault owners and exercise stalls,
live-bypass capture, continuation ownership, and recovery. They check physical
upper bits, sign-extended PCs, normalized speculative targets, sequential boundary
faults, and fault suppression of speculative predictions. Distinct saved lookup
rows and ordered parcel histories check reconstructed checkpoints through stalls,
straddles, and XLEN wrap. Inconsistent histories, invalid producer indices, and mismatched fetch-PC/fault-VA
pairs must fail admission. The H variant also
checks guest provenance and rejection of an out-of-range successful compact PC.
Local repair clears block ownership on the following cycle; it must
not clear the lineage of the same edge's accepted packet.
`superscalar-assembly-three` checks all eight instruction-length triples at every
halfword start, compact high PCs and XLEN wrap, source-block direction ownership,
backpressure, third-position prediction repair and continuation faults (including
guest provenance), single-RAS-action prefixes, and clear over live bypass.
`superscalar-frontend-three` runs the existing frontend recovery oracle with
three-entry output and checks three instructions returned from one eight-byte block.
`rv2wide-frontend-prediction` supplies fixed-cycle instruction-cache responses
through public ports. It checks same-cycle S1 prediction and S2 target offers
for both slots,
compressed/backward jumps and cold/stale straddles, stale jump/conditional targets,
preservation of correct S1 predictions, stalled packet acceptance and corrected
cursors, younger fault/replay cancellation, and architectural redirect priority.
Keep SRAM/refill integration in the production RV2Wide/RV3Wide fetching fixtures
and event-lineage integration in `rv2wide-fetch`.

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
fixture ownership, confirm the rsim runner's `--group cores-components --list`
includes `rv2wide-core`, `rv2wide-fetch`, and `rv2wide-rv32`. Mini/Simple/Tiled integration uses the existing shape harnesses
and their normal FESVR loading and BootROM path. Run `boot-test` and `isa-smoke`
with `SOC=mini-rv2wide-rv64imacb` and `SOC=simple-rv2wide-rv64imacb` for platform
changes. Tiled RVA23 additionally runs the ordinary eight-hart benchmark and
model-checked litmus smoke paths; its trace selection uses the same event pass.
Keep CI enrollment in `sims/test-configs.txt` and workload selection
in the shared shape/ISA policy, not in the processor configuration.
