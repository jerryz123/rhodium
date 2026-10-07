<!-- Documents RV2Wide's translated fetch frontend, dual-issue execution, and precise retirement interfaces. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV2Wide

RV2Wide is an in-order dual-issue processor under construction. `RV2Wide`
fetches instructions through a shared L1I and executes through the shared L1D;
it supports Bare/Sv39 addressing with shared M/S/U CSR/trap state. `RV2WideCore()` remains the
independently usable RR-through-WB execution slice. It executes RV64IMACB, optionally RV64IMAFDCB:
integer arithmetic, word arithmetic, LUI/AUIPC, branches, JAL/JALR, and scalar
loads/stores, including retained misaligned accesses in cacheable normal memory.
It also executes Zicond and Zimop, plus Zicsr, ECALL/EBREAK, MRET/SRET, WFI,
SFENCE.VMA, FENCE, and FENCE.I at WB.
See [DEVELOPING.md](DEVELOPING.md) for ownership and validation.

For complete BootROM/FESVR execution, select `mini-rv2wide-rv64imacb` or
`simple-rv2wide-rv64imacb` through the existing [simulation harness](../../sims/README.md):

```sh
make -C sims simulator SOC=mini-rv2wide-rv64imacb
make -C sims isa-smoke SOC=simple-rv2wide-rv64imacb
```

Both use the exact lean RV64IMACB preset, M/S/U, and Sv39. Mini has 32-set,
direct-mapped L1s; Simple has 64-set, four-way L1s. Both retain the five-stage
feed-forward multiplier. `RV2WideConfig` owns this architectural projection and
private-cache geometry; `RV2WideHart` starts once at the platform reset vector
after reset. The shared SoC BootROM performs normal FESVR entry publication and
ACLINT release. The corresponding `mini-rv2wide-rv64imafdcb` and
`simple-rv2wide-rv64imafdcb` selections add F/D and compressed FP loads/stores.
No RV32, vector, H, or Tiled selection is provided.

## Entry point

```rhdl
import:
  lib("cores/rv2wide/core.rhdl") open
  lib("cores/rv2wide/bundles.rhdl") open

inst core(RV2WideCore())
```

For the fetching core with both shared caches, import
`cores/rv2wide/rv2wide.rhdl` and `cores/cache/config.rhm`, then instantiate
`RV2Wide(CacheConfig(64, 2), ~chi: config)`. Optional `~instruction_cache`
selects different L1I geometry; otherwise both use the supplied geometry.
`RV2WideConfig(~btb_entries: 32, ~ras_entries: 6)` selects predictor capacities;
setting both to zero selects the sequential-fetch baseline.
Connect separate `instruction_chi` RN-I, `data_chi` RN-F, and `uncached_chi` RN-I
endpoints with distinct `instruction_node_id`, `data_node_id`, and
`uncached_node_id` inputs. Pulse
`start: Valid(Bits(64))` with the initial physical PC after reset. `halted`
is initially true and clears at start; starting again requires reset. Connect
architectural `interrupts: RiscvInterrupts()`, `hart_id`, and `time_counter` inputs.
`sleeping` reports WFI wait, not permission to start again. A reset must reset
the external memory-service epoch too.

Retirement, completion, redirect, and issue-count outputs remain observable.
Branch prediction corrections, memory replays, traps, and privilege returns redirect fetch
internally. Software programs mtvec/stvec and supplies its trap handler.
`cores/rv2wide/cache.rhdl` owns the standalone `RV2WideL1D` adapter.

When using `RV2WideCore()` directly, provide `instructions: Decoupled(RV2WidePacket())`. Each packet has a `count`
of one or two and that many valid `entries`, oldest first. Entries contain a
64-bit `pc`, canonical 32-bit `instruction`, original `raw_instruction` (zero-extended
for C), length-derived `sequential_pc`, `compressed_illegal`, and optional fetch-fault
cause/address. Direct packet producers supply all of this metadata; a 32-bit
instruction normally uses the same raw/canonical bits and `sequential_pc = pc + 4`.
Clear `fault.valid` and `compressed_illegal` for successful fetches; instruction
bits are ignored for fetch faults.
Each entry also supplies `prediction: BranchPrediction(XLen.X64)` and
`speculated_ras_action: RasAction`. A nonpredicting packet source clears
`prediction.valid` and uses `RasAction.None`. Prediction metadata belongs to
the instruction, not its packet position, and remains attached after compaction.
The eight-entry instruction buffer retains
unconsumed instructions and coalesces adjacent packets after partial issue.
The source must follow redirects and supply instructions in program order;
the standalone slice does not fetch instructions itself. `fetch_flush: Pulse()`
immediately cancels younger fetch work, even while a fault is waiting for older
memory work to drain. Stop fetching on that pulse until a redirect or explicit
restart supplies the next PC; a simultaneous redirect takes priority.
Connect `instruction_invalidate: Pulse()` to instruction-cache invalidation;
it accompanies the successful WB FENCE.I successor redirect and is distinct
from a speculative fetch flush.
The standalone core exports successfully retired `branch_update` records,
`predictor_restore` recovery pulses, and `predictor_clear` context-invalidation
pulses for a connected predictor; `RV2Wide` connects these internally.

## Floating point

Select `RV2WideConfig(~floating_point: FloatingPointProfile.D)`; the default
remains integer-only. RV64D implies F. Controls, decode, the three-read/two-write
FPR file, numerical execution, boxing, and flag behavior come from
[`cores/fp/`](../fp/README.md).

Add Zfa 1.0 with
`RV2WideConfig(~floating_point: FloatingPointProfile.D, ~zfa: #true)`.
Zfa requires FP and defaults to disabled; the existing lean SoC presets are
unchanged. The ISA/hart description and UDB projection publish Zfa only when
selected. It reuses the existing FP execution and writeback paths for single-
and double-precision immediates, rounding, min/max, quiet comparisons, and
double-to-word modulo conversion.

Select `~half_precision: HalfPrecisionProfile.Zfhmin` or
`HalfPrecisionProfile.Zfh` on the same D-enabled configuration to add half
precision; the default is `None`. Zfhmin provides FLH/FSH, bit moves, and
single/double conversions. Zfh additionally provides half arithmetic, fused
operations, comparisons, classification, and integer conversions. With Zfa,
full Zfh also enables its half-precision operations. Half values are NaN-boxed
in the existing 64-bit FPRs; loads/stores retain their natural two-byte width,
including through split accesses. ISA and UDB declarations follow the selected
subset. These options do not change the lean SoC presets.

One FP instruction can issue from either age slot alongside an independent
integer instruction. RR snapshots all three FPR operands and checks separate
GPR/FPR dependencies. Fixed operations launch in EX; WB authorizes their
architectural result. Squashed arithmetic drains without writing a register,
setting flags, or dirtying FS. Divide/sqrt launches only at WB. A variable FP
operation does not pair with a memory or integer multiply/divide operation;
independent ordinary integer work can still pair.

`RV2WideCore(~fp_timing: FpExecutionTiming(...))` uses the execution service's
declared latencies, floored to the two-cycle EX-to-WB distance. It does not assume
all FP returns take two cycles. RR reserves a fixed return slot before launch.
An FP-to-GPR result at its own WB uses that instruction's ordinary write port;
a later result reserves the younger slot's GPR write edge and forwards directly
to RR. GPR storage remains two-write-port. Unpredictable integer returns retain
their existing completion pipeline.

FPR arithmetic and loads use separate write ports with same-cycle forwarding.
FP loads/stores reuse the scalar cache, translation, and split-access paths.
Accepted load/divide owners survive flushes; precise trap/interrupt entry and
serializing CSR operations drain older FP effects. FPR writes and completed
exception flags update the shared FS/fflags/frm state, never speculative launch.

## Instruction fetch

The current frontend/caches implement this pipeline:

```text
IF1: PC / L1I array admission -> IF2: ITLB / physical permission / tag resolution
  -> ID: 64-bit result / ordered packet assembly -> issue window -> RR -> EX -> MEM -> WB
```

Each aligned eight-byte block supplies two to four mixed 16/32-bit instructions,
low address first. Restart may select any halfword. The shared compressed expander
supplies canonical instructions to the ordinary composed decoder, while original
bits and sequential PCs remain attached through retirement and delayed completion.
An instruction may cross a block, cache line, or page; each block is independently
translated and checked. The assembler retains a lone unfinished halfword until
the following block arrives, including across a fetch replay.

Consecutive hits can sustain two instructions per cycle. Three Flow-owned block
slots reserve space for in-flight SRAM reads and excess compressed instructions.
Empty-buffer bypass avoids adding a hit-path stage. The assembler sends up to two
instructions per cycle to the existing eight-entry issue window; downstream stalls
retain both blocks and parcel position. Admission uses registered block credits,
not the current issue decision. The execution slice still exposes
`instruction_capacity: Bits(4)` for direct packet producers.

L1I misses restart the failed fetch attempt and discard younger attempts,
preserving older buffered instructions. A MEM prediction correction or WB redirect clears speculative fetch
and issue storage, but accepted CHI refills continue draining. Wrong-path
errors cannot become architectural faults. A failed block produces one fault
token at the first requested instruction PC, not an illegal instruction derived
from undefined data. For a straddling instruction, its original PC identifies the
faulting instruction and the continuation address identifies the faulting portion.
WB preserves age ordering and drains older accepted data
transactions before reporting the fault.

Translation, physical permission, and address-width checks happen before cache resolution.
Only executable, instruction-cacheable, idempotent memory is fetched: coherent
RAM uses `ReadOnce`, immutable ROM uses `ReadNoSnp`. Other regions report an
instruction access fault without CHI traffic. Odd starts report instruction-address-
misaligned faults.

## Branch prediction

The frontend uses the shared 32-entry BTB with eight-byte lookup and a six-entry
RAS. S0 selects the earliest predicted-taken instruction at or after the cursor
and requests its target next, without adding a fetch stage. A 32-bit instruction
at byte six requests its continuation block before the target.

Assembly checks actual boundaries, instruction length, and control-flow encoding.
It emits the prefix through the predicted branch and discards only its fall-through
suffix. On a BTB miss, direct JAL/C.J targets and RAS-backed returns redirect from
assembly and discover a BTB entry. Stale predictions invalidate the exact entry
and restart after the accepted prefix. Local repair discards younger fetch blocks,
not instructions already accepted by the issue window.

MEM compares predicted and actual successors. Correct taken predictions retain
target-stream work, including a target instruction paired in the younger lane.
Wrong direction, wrong target, or unmatched RAS action recovers once at MEM;
older WB recovery has priority. Resolved BTB training and resolved RAS updates
come only from successfully retiring branches, separately from assembly discovery.
The speculative stack changes once per accepted complete
call/return; two actions are emitted in separate packets. Recovery restores the
resolved stack and reconciles through the surviving branch's WB retirement.
Instruction invalidation and translation/context invalidation clear predictors.
The BTB has local two-bit direction counters, not global history or ASID tags.

## Execution and ordering

Zicond's CZERO.EQZ/CZERO.NEZ use each slot's shared ALU and ordinary GPR
interlocks/forwarding; the condition observes the full 64-bit rs2. All 32 MOP.R
and eight MOP.RR encodings implement Zimop's zero-result behavior. Their encoded
source fields are ignored, but destination reservations and x0 rules still apply.
MOPs are ordinary dual-issue ALU work, not serializing system instructions.

Current implementation:

```text
instruction packets -> eight-entry buffer -> RR -> EX -> MEM -> WB
                                            ^     |      |     |
                                            +-----+------+-----+ forwarding
```

RR consumes zero, one, or two instructions. Independent ALU operations can
issue and retire two per cycle. A slot-0 integer ALU producer can pair with a
slot-1 ordinary integer load that uses its result as the base address and has zero
immediate offset. EX sends the producer's result directly to the load lookup,
without a second dependent address addition or an extra pipeline stage.
This includes a load overwriting the producer's destination. Other same-group
RAW dependencies split the pair; stores, atomics, nonzero-offset loads, and
memory, M, FP, CSR, or control-transfer producers do not use this bypass.
Same-destination writes can pair when the older writer is guaranteed to use
normal WB, even if the younger writer completes later. An older memory, M, or late FP-to-GPR
writer can defer its result and still splits a same-destination pair. An
instruction that cannot pair stays at the head for the
next cycle. One branch resolver serves either age slot, and two branches cannot
issue together.
The shared `cores/alu.rhdl` and `cores/branch-resolver.rhdl` own execution.
Each slot uses one composed structured decoder, with the shared component
relations joined to RV2Wide operand/writeback controls.

The complete B extension (Zba, Zbb, and Zbs) uses that same ALU and EX timing in
either slot. Independent B instructions can dual-issue, and their results forward
through the ordinary integer paths. Unary/immediate instructions do not create a
false dependency on their encoded subopcode or shift amount. `.UW` instructions
zero-extend rs1's low word before full-width arithmetic; word rotates and counts
have their own word-result semantics. There is no separate bit-manipulation unit
or additional execution stage. The CSR bank declares Zba/Zbb/Zbs and the MISA B bit.

EX, MEM, and WB use feed-forward Flow `ValidPipe` registers. Forwarding searches
both lanes, preferring the youngest older producer. A matching unavailable
result blocks instead of exposing stale register data. There are four GPR reads
and two GPR write ports. A shared scoreboard
reserves accepted slow-load and multiply/divide destinations at WB. RAW and WAW interlocks cover
both older pipeline lanes and outstanding destinations. Returned values forward
from completion arbitration through the reserved write edge. Independent work
continues while results are outstanding.
Register state resets to zero; x0 ignores writes but does not suppress memory
accesses or their faults. Successful same-group writes to one register preserve
both retirement records, but only the younger value updates the register file.
If the younger instruction faults, replays, or defers its write, the older write
still updates the register file. Outstanding older deferred writes remain WAW
interlocked until their RF write edge.

`retired[2]: Valid(RV2WideRetirement())` reports the successful ordered prefix
at WB, including PC, encoding, destination, write enable, and value. `issued`
and `retired_count` report counts of zero, one, or two for the current edge.
Outputs have no backpressure. `deferred` means a memory or multiply/divide operation was
accepted at WB and its response will arrive on `completed`, rather than being
reissued or retired again. For a deferred GPR result, `write` describes the eventual
GPR write and retirement `data` is unspecified. `completed` preserves the
instruction identity and reports the returned value; store acknowledgements
have `write` false. Non-writing data is unspecified on either interface.

## Multiply and divide

All RV64M operations, including MULW, DIVW/DIVUW, and REMW/REMUW, use the
shared physical control relations. One M instruction may issue per group and
may pair with independent ALU or branch work. Memory and M instructions split
the group so WB allocates at most one deferred destination per cycle.

The five-stage shared multiplier launches from EX at one operation per cycle.
Its owner reaches WB with the instruction, where retirement authorizes the
result. Rejected owners release their reservation; the physical product continues
without an architectural write. Eight reserved result slots absorb completion
arbitration, with admission headroom for RR-to-EX work. The iterative divider
accepts only at WB; a busy divider causes replay before acceptance. Word divide
operands are sign- or zero-extended independently of final word sign extension.

Load, multiply, and divide completions use one round-robin Flow arbiter and the
existing younger-slot reservation pipeline. Completion order may differ from
retirement order; each result carries its original instruction identity. The
scoreboard blocks reads until the result returns and younger writes until the
reserved second-port write. Independent instructions continue. Accepted work survives redirects, and precise
trap/interrupt entry drains all accepted results and completion-pipeline writes.
Division by zero and signed overflow return the architectural M results.

## Atomic memory operations

RV64A supplies LR.W/LR.D, SC.W/SC.D, and all nine W/D AMOs through the shared
coherent L1D. The composed decoder selects cache ownership, AMO function, and
natural transfer width; there is no second atomic execution unit. LR and AMO
word results sign-extend their prior 32-bit value. SC returns zero on success
and one on failure. x0 suppresses only the GPR write, not an atomic effect or fault.

Atomics can occupy either issue slot and pair with independent integer work.
They drain older memory before issue, authorize only at WB, and prevent younger
memory retirement until their response is consumed. This conservative ordering
supports every aq/rl combination; independent integer instructions can continue.
Responses use the same retained owner FIFO, scoreboard, completion arbiter, and
two GPR write ports as loads and M operations. Accepted owners survive redirects.

LR/SC reservation matching, invalidation by stores, AMOs, snoops, and replacement,
and unique ownership belong to the shared L1D. Atomics require naturally aligned
addresses in atomic-capable coherent cacheable RAM. LR requires read permission;
SC and AMO require write permission as well. Noncacheable/device accesses fault
without issuing CHI traffic. Misalignment and permission failures retain the
original VA and use the architectural load versus store/AMO exception classes.
The CSR bank publishes the A bit in MISA.

## Branch recovery

Branches resolve in EX and correct mismatched successors or RAS actions from MEM.
An older correction suppresses the younger slot; a younger correction preserves
the older peer. A correct taken prediction keeps the target-stream younger slot. Both the
branch and any older peer still retire at WB. MEM recovery clears younger EX,
RR, and fetch work, not older WB work or accepted memory transactions. WB faults
and replays take priority over simultaneous MEM recovery. An older same-group
memory operation can still fail authorization at WB on the following cycle;
that recovery overrides the earlier branch target and prevents branch retirement.
Every redirect rejects instruction admission on that edge. A not-taken branch
corrects a predicted-taken path to its sequential PC. A corrected branch does not
redirect again at WB.

## Pipelined memory and deferred completion

The integrating MMU uses separate ITLB and DTLB banks and one shared walker.
For data, EX launches the virtual page-offset SRAM lookup. MEM translates the
registered VA and combines its physical tag and permissions with the SRAM result;
translation never gates EX array admission. WB authorizes stores and slow
transactions. A speculative DTLB miss reports Slow without launching a walk;
its WB retry requests the translation. An older WB request takes priority over
a concurrent speculative MEM lookup, which replays.
`RV2WideL1D` exposes `pipeline_lookup: Valid(CachePipelineReq(XLen.X64))`
for the early index and `pipeline: CachePipelineAccess(XLen.X64)` for next-cycle
physical resolution and following-cycle store authorization. The MMU provides
the fixed-latency `RV2WidePipelineAccess` boundary used by the execution core.

The CSR bank supplies satp, current privilege, and mstatus (including MPRV,
SUM, and MXR). SFENCE.VMA serializes at WB, invalidates both banks, cancels stale
walk publication, and refetches younger instructions. It conservatively flushes
all entries even for address/ASID-selective encodings. Accepted PTE replies
continue draining across cancellation. Ordinary branch/fetch/WB replays do not
cancel walks; pending WB translations take priority over further fetch walks,
and a fetch walk never owns the physical data port for its entire lifetime.
Faults retain the original virtual address through retirement in either slot.
Sv39 uses software-managed A/D bits; absent A/D permission produces a page fault.

One memory operation may issue per group, in either age slot. The memory service has
the same lookup-versus-authorization split as RV5Stage:

```text
EX -- Valid lookup --> memory service -- fixed MEM result --> WB
                                                            |
                        hit-store commit / slow admission <--+
                                                            |
              accepted owner FIFO <-- WB acceptance --> scoreboard set
                      |                                      |
ordered slow response +--> result projection --> reserve younger RR slot
                                            |
                             unflushable completion pipe --> shared WB write
```

`pipeline: RV2WidePipelineAccess()` carries an EX request and the next cycle's
MEM result. Its byte address, positioned store data, and byte mask describe
one access within an aligned eight-byte beat. A speculative lookup must not
allocate, mutate memory, or perform device reads. The service returns:

- `LoadHit`: an aligned raw beat, normalized by the shared `LoadGen` for normal
  MEM forwarding and WB writeback. Independent hits sustain one per cycle.
- `StoreHit`: a candidate retained by the service for exactly the following WB
  cycle. Only `commit & commit_ready` authorizes its effect; rejection replays.
- `Slow`: WB attempts the separate authorized transaction path.
- `Replay`: resource contention retries the instruction, without slow admission.
- `PageFault` or `AccessFault`: WB reports the appropriate load/store cause and
  original byte address. Missing MEM feedback is treated as replay.

LR/SC/AMO use Slow after successful checks, never a speculative load/store hit.

`memory: RV2WideMemory()` accepts WB `Decoupled` requests. Admission is
non-speculative and resolves synchronous exceptions **before acceptance**:
`fault.valid` supplies a Fault resolution, including its precise address, and
the service must deassert request readiness. Otherwise a transfer irrevocably
owns one successful response. Unaccepted offers are withdrawn and replayed.
Ordinary loads return an aligned raw beat; LR/AMO return the normalized old
value and SC returns status. Store response data is ignored.
The service must preserve architectural memory ordering and prevent stale hit
results from bypassing older overlapping writes, using forwarding or replay.
Device accesses must use Slow, never a speculative hit.
Requests retain `CacheOperation`, AMO function, natural `MemoryWidth`, and
byte masks. Ordinary store data is positioned within its aligned beat; atomic
operands are unshifted, and the cache performs their natural-width projection.
Uncached transport must not widen a byte access into a full-beat device read.
`memory.ordered_busy` blocks younger memory retirement, including already
resolved cache hits, while an ordered transaction awaits completion. Independent
integer instructions may continue. This signal must describe retained service
state, not depend on the current request valid or pipeline commit.

Four transaction contexts can await responses. The response unit joins each
response with its retained owner using Flow, independently of speculative flushes.
Full context storage replays a new attempt before acceptance; it never cancels
older requests. Accepted loads, including x0 loads, and store acknowledgements
continue draining across younger branch/replay redirects. `memory.drained`
must also include internally buffered committed hit stores.

A returned load needing a GPR write reserves the younger issue slot at RR.
The older instruction may still issue; the unconsumed younger instruction stays
in the window. The response follows an unflushable three-cycle Valid pipeline
to the reserved younger WB write port. Thus WB never needs to backpressure
instructions, and a delayed response cannot collide with a previously issued
younger-slot write. Store acknowledgements and x0 loads do not reserve a write
slot. LR/AMO/SC use the same reservation rules for their optional GPR result.
The scoreboard remains set until the actual WB write, and fault drain
includes completions already in this pipeline.

Fault entry waits for accepted memory work to drain. The core immediately
squashes younger instructions and retains the fault report, then emits it after
all older effects and deferred GPR writes complete. This also covers an accepted
older load paired with a faulting younger instruction. Ordinary cached misses
do not globally serialize independent ALUs or cache hits; ordered uncached
transactions block younger memory operations only.

## CSR, traps, and interrupts

The shared `cores/csr` bank owns architectural state and permission checks;
RV2Wide owns precise ordering. System instructions issue alone only after older
pipeline work and accepted deferred operations (including GPR completion writes)
drain. They execute once at WB. Younger instructions may execute speculatively,
but WB system recovery squashes them and overrides same-cycle MEM branch recovery.
A successful CSR instruction refetches its successor; returns select mepc/sepc.
CSR register-source and immediate-source forms retain the architectural source
index, so CSRRS/CSRRC with a nonzero source register containing zero still attempt
a write, while a zero source index suppresses it.

WB retires only the successful prefix: an older instruction still retires if its
younger peer faults. `minstret` receives an explicit count of zero, one, or two;
delayed completion is not a second retirement. Counter writes take priority
over that instruction's count. Interrupts enter before the oldest unretired WB
instruction, or at the saved next PC when the pipeline is empty. Synchronous
faults already selected at WB take priority. Entry waits for accepted work to
drain, and a canceled interrupt restarts at its retained boundary without losing
an instruction. No handler observes an outstanding older GPR write.

WFI retires once and flushes younger work, then sleeps until a locally enabled
interrupt is pending. A globally enabled interrupt takes the normal trap path;
otherwise fetch resumes at WFI's successor without an interrupt trap. Trap entry,
return legality, delegation, and direct-vector semantics come from the shared
bank. The integrated top connects the bank's privilege and translation state to
the shared Bare/Sv39 MMU.

FENCE conservatively orders all predecessor/successor classes: it issues alone
after older cache stores, accepted transactions, and deferred GPR writes drain.
FENCE.I additionally invalidates instruction-cache residency and prevents old
refills from installing, flushes younger fetch/pipeline work, and refetches its
successor. Both actions occur once at nonspeculative WB. Instruction snapshots
are replenished through coherent `ReadOnce`; system integration must supply the
latest coherent data, including dirty data held by L1D.

The boundary requires admission-certified aligned transactions, not an arbitrary
bus that may raise a late synchronous exception. Split accesses use a separate
retained-retirement contract; they cannot reuse the successful deferred-response
contract.

### Ordinary misaligned accesses

Naturally misaligned integer loads/stores use
`split: RiscvSplitAccess(XLen.X64, Bool)` on the standalone execution slice.
The integrated top connects this to the shared `RiscvMisalignedEngine` through
its MMU. The request retains the original virtual byte address, natural width,
signedness, and unshifted store data. The one final Valid response carries either
the assembled load value or a page/access fault and its exact virtual address.

WB retains the instruction, flushes younger work, and drains older accepted work
before split dispatch. A successful older peer may retire immediately; the split
instruction retires exactly once on successful completion and redirects to its
successor. A late fault enters the existing precise trap path without writing
the destination or retiring the instruction. No deferred RF owner or third
register-file write port is introduced.

Each aligned eight-byte fragment is translated, checked, and completed before
the next fragment issues. Only cacheable, idempotent normal memory is supported;
misaligned device/uncached accesses report access faults without device effects.
The first fragment reports the original VA on fault; the second reports the
next aligned word's VA. An accepted store prefix remains visible if the second
fragment faults. Split accesses are not atomic, and accepted prefixes are never
replayed or rolled back. LR/SC/AMO still require natural alignment.

## Resolution and restart boundary

`memory_stage[2]: Valid(RV2WideInstruction())` identifies each current MEM token.
The corresponding `resolution[2]: Valid(RV2WideResolution())` may qualify it in
the same cycle with Continue, Fault, or Replay. This is a synchronous stage
qualification boundary, **not** an asynchronous memory response interface.
Absent qualification means Continue. Inputs for nonexistent or WB-squashed
tokens have no architectural effect. This cut uses the boundary to exercise
ordering before connecting a real memory subsystem.

An internally detected instruction fault takes precedence over external
qualification. Faults and replays never retire or write their destination.
WB chooses the oldest fault/replay, allowing a preceding successful instruction
to retire exactly once. `redirect: Valid(RV2WideRedirect())` reports recovery:

- Continue: a prediction correction at MEM; `target` is the actual successor
  (the taken target or sequential PC). This
  is speculative recovery, not a retirement notification.
- Replay: restart at `pc`; the instruction has not committed.
- Fault: a synchronous trap; `pc`, cause, and fault value are reported, and
  `target` is the architectural trap vector.
- System: WB serialization/return recovery, successful split-access resume,
  WFI wake, or an interrupt boundary.
  `target` is the selected successor, return PC, or trap vector. Cause/value are
  meaningful for an interrupt trap (cause has its interrupt bit set).

Instruction PCs require two-byte alignment. JALR clears bit zero; compressed
JALR writes `pc + 2`, while a full-width jump writes `pc + 4`. The shared CSR bank
advertises C and preserves bit one in exception PCs and privilege-return targets.
Unmatched canonical or compressed encodings report an illegal instruction fault
with the original encoding. Reset clears queued/pipelined
work, completion ownership, scoreboards, and architectural registers. It is a
coordinated epoch boundary: the memory service must reset with the core and
must not return pre-reset responses afterward.

## Pipeline event tracing

Mini/Simple bindings support the existing event-trace compile pass with `TRACE=1`.
It is independent of `COSIM=1`; both can be enabled on the same simulator.
See the [simulator commands](../../sims/README.md#export-soc-events-to-perfetto).

Each age slot has separate `core/s1.rr.slotN`, `core/s2.ex.slotN`,
`core/s3.mem.slotN`, and `core/s4.wb.slotN` tracks with raw instruction and PC
captures. The stage prefixes order the execution pipeline in Perfetto. RR records
actual issue and blocked offers. WB records successful retirement only, including
the later retirement of a retained split access; traps, replay, and speculative
multiply launch are not retirement.

```text
fetch S0 → S1 → S2 → assembly packets → issue window → RR[0/1] → EX[0/1] → MEM[0/1] → WB[0/1]
                                                         EX multiply ──authorization──→ completion arbitration
                                                                      WB load/divide ──→ completion arbitration
                                               completion arbitration → three-stage completion pipe → deferred RF write
```

This illustrates the current implementation. Packet ownership survives compacting,
one/two-slot consumption, compressed assembly, and flush. Load owners survive in
their queue, multiply owners traverse the feed-forward authorization path, and
divide owners remain with the shared divider until accepted completion.
`core/s4.wb.deferred` marks a deferred service completion reaching WB. Its ancestry
follows the retained service and completion pipeline directly from the instruction's
EX multiply launch or WB load/divide acceptance, without intermediate service events.
RR inherits the one or two `frontend/s2.outcome` blocks contributing to its
assembled packet through the assembler and issue window. Packet assembly has
no separate trace checkpoint. RR stall observations share their corresponding
`core/s1.rr.slotN` track and remain named `stall`. Frontend stage numbering is
local to fetch.
RR captures two boolean stall categories: `data_hazard` covers RAW/WAW
dependencies, including same-group conflicts; `structural_hazard` covers
resource capacity, shared execution/write ports, serialization, atomic draining,
and fault/illegal-instruction ordering. A younger offer also inherits the older
offer's blocking categories. Both can be true together. Changing categories
split the continuous stall slice; issued RR events have both false.
Shared cache/CHI events retain their existing annotations. Partial tracing still
reports unmodeled fetch-cursor and external response provenance; it does not
invent ancestry from equal PCs or reused transaction IDs.

## Deliberate limits

There is no guest translation. Optional FP supports F/D, Zfhmin/Zfh, and Zfa.
Misaligned accesses to devices or uncached memory are deliberately unsupported.
The SoC bindings publish lean RV64IMACB or RV64IMAFDCB presets, not RVA23. Mini/Simple
bindings support target-selected [Sail cosimulation](../../sims/cosim/README.md)
with `COSIM=1`; CI enables it on their existing ISA-smoke rows. Shared ISA
descriptors remain in `riscv/`; named-core execution policy remains here.

The MMU checks the CHI physical map before physical tag resolution; the data-cache
adapter rechecks authorized admission. Early array indexing uses only page-offset
bits and causes no allocation or mutation. Coherent, cacheable, idempotent RAM
uses aligned full-width cache beats. Mapped noncacheable RAM and devices instead
use exact-address, exact-width `ReadNoSnp`/`WriteNoSnpPtl` transactions through
the shared IOMSHR and uncached CHI engine. They drain older cached work before
admission, permit only one ordered transaction, and block younger memory effects
until its response is consumed. Speculation never issues a device transaction.
PMA permission/mapping failures trap without CHI traffic, with the original VA;
page-table reads remain restricted to readable, cacheable, idempotent RAM.

The core retains load extension and completion ownership. Accepted traffic drains
across redirects, and the two-port writeback policy is unchanged. As with the
existing shared ordinary-memory path, admitted transactions must complete
successfully; late CHI errors are protocol assertions, not implemented precise
bus-error traps.
