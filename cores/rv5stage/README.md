<!-- Defines RV5Stage's public microarchitecture, system boundary, and supported behavior. -->

# RV5Stage

RV5Stage is a single-issue, in-order, five-stage RISC-V processor implemented
as one authoritative Rhodium RTL circuit. A required `xlen :: XLen` host
parameter selects RV32 or RV64 without admitting arbitrary integer widths.
Optional floating-point, half-precision, and compressed-instruction parameters
specialize the same scalar pipeline with a parallel FP execution engine and
variable-length Fetch.

Core-specific decode, architectural state, pipeline policy, MMU, and private L1
caches live here. Reusable execution components remain directly under
[`cores/`](../).

Contributors changing the core should read
[`DEVELOPING.md`](DEVELOPING.md).

## At a glance

| Property | Contract |
|---|---|
| Pipeline | Fetch, Decode, Execute, Memory, Writeback |
| Issue and retirement | Single issue; ordered WB commit |
| Pipeline boundaries | Producer-owned fetch queue and elastic IF/ID, then feed-forward ID/EX, EX/MEM, and MEM/WB |
| Deferred work | Loads, atomics, multiply, divide, and FP results may complete after their scalar token retires |
| Integer widths | RV32 and RV64 selected by `XLen.X32` or `XLen.X64` |
| Floating point | Disabled by default; RV32F or RV64D, with optional Zfhmin, Zfh, or Zfa |
| Address translation | Bare for RV32; Bare or Sv39 for RV64 |
| Private caches | Separate configurable L1I and blocking write-back L1D; fixed 64-byte lines; demand-priority Zicbop admission |
| External memory | Separate instruction and data CHI RN-F channels plus a shared uncached RN-I channel |

The integer decode includes RV32I/RV64I, A, B, M, Zicond, Zimop, Zicsr, Zifencei, and
the supported privileged instructions. Optional Zicbop decode turns its
otherwise legal `ORI x0` hints into best-effort WB-authorized prefetch events.
Optional Zicbom adds `CBO.CLEAN`, `CBO.INVAL`, and `CBO.FLUSH`; enable it with
`RV5StageExtensions(~zicbom: #true)`. It remains disabled by default.
Optional C expansion follows the
selected XLEN and FP profile; RV32F or RV64F and RV64D rows, plus optional
Zfhmin, Zfh, and Zfa rows, are added only by their matching FP specialization. Zicntr and Zihpm
views come from the CSR block rather than instruction rows. The
[`decode guide`](decode/README.md#select-a-decode-specialization) owns the exact
specialization matrix and catalog composition. RV32D and an RV64F-only core are
deliberately rejected.

## Cache-block and reservation bounds

Every RV32 and RV64 profile advertises **Zic64b 1.0.0** and **Za64rs 1.0.0**,
including profiles without CMO instructions. These describe existing hardware;
they add no opcodes, CSRs, `misa` bits, or enable switches.

Zic64b fixes the naturally aligned cache block at 64 bytes across L1I, L1D,
refill/writeback, and cache-block operations. Cache capacity and associativity
remain configurable, but line size does not. Za64rs bounds a contiguous,
naturally aligned reservation set to **at most** 64 bytes, not exactly 64.
RV5Stage reserves the naturally aligned LR access (4 bytes for LR.W, 8 for
RV64 LR.D) and requires SC to match its physical address and width. Conservative
same-line invalidations may also cause SC failure; the
[L1D reservation contract](dcache/README.md#lrsc-reservation) describes them.
This size guarantee does not establish Ziccrse's LR/SC forward-progress guarantee.

Profile-derived device trees advertise `zic64b` and `za64rs`. UDB includes their
exact versions, the implied weaker `Za128rs` bound, a 64-byte `CACHE_BLOCK_SIZE`
even without CMO decode, and the access-sized `LRSC_RESERVATION_STRATEGY`.
Definitions follow the [ratified profiles](https://docs.riscv.org/reference/rvb23/v1.0/rvb23.html).

UDB 0.1.16 has an applicability inconsistency for CMO-free profiles: Zic64b
requires `CACHE_BLOCK_SIZE`, but that database defines the parameter only for
Zicbom/Zicbop/Zicboz. The projection retains the truthful hardware value;
validation of those CMO-free configurations requires a corrected UDB definition.
The concrete SoC profiles enable CMOs and are unaffected.

## Coherent main-memory guarantees

`RV5StageExtensions(~ziccif: #true, ~ziccamoa: #true)` advertises **Ziccif
1.0.0** and **Ziccamoa 1.0.0** in device trees and UDB. Both default to false
for custom integrations; MiniSoC, SimpleSoC, and TiledSoC enable both.
These are integration guarantees, not decoder switches, and do not change `misa`.

Ziccif requires every cacheable coherent main-memory region to support instruction
fetch, with atomic naturally aligned power-of-two fetches through 32 bits
(`min(ILEN, XLEN)` here). Ziccamoa requires AMOArithmetic in every such region:
all nine A-extension AMOs at word width, plus doubleword width for RV64.
`RV5Stage` checks the corresponding executable and atomic PMAs during elaboration;
its CHI configuration already requires coherent HN-F Homes and complete cache lines.
BootROM and device HN-I regions are outside these requirements.

These claims do not provide Ziccrse forward progress, misaligned access support,
or automatic instruction-cache synchronization; self-modifying code still needs
the architectural instruction-synchronization sequence. Integrators remain
responsible for the external Home/memory coherence contract. See the
[focused validation and its limits](DEVELOPING.md#focused-validation).

## Data-independent timing (Zkt)

Every RV32 and RV64 profile advertises **Zkt 1.0.1**, including FP-enabled
specializations. This is an intrinsic execution guarantee, not an optional
decoder feature: it adds no instruction, CSR, or `misa` bit. Profile-derived
device trees advertise `zkt`; the UDB projection includes the exact version.

Implemented instructions in the [architectural Zkt scope](../../riscv/isa/zkt.rhm)
have operand-independent execution latency. The ALU is combinational; multiply
uses a fixed capture/preparation/iteration sequence without operand-based early
exit. Forwarding, scoreboards, issue, and completion arbitration depend on
instruction/register metadata and availability, not arithmetic operand values.

This does **not** promise identical elapsed cycles under different environments:
fetch stalls, older work, and completion contention may delay execution. The
comparison holds code, initial control state, and external scheduling fixed
while varying covered operands. Loads/stores, conditional branches, divide and
remainder, FP instructions, CSR accesses, and fences are outside this contract.
It is not a guarantee against cache-address leakage, speculation attacks, power,
or electromagnetic side channels. See the
[RISC-V Zkt definition](https://github.com/riscv/riscv-unified-db/blob/main/spec/std/isa/ext/Zkt.yaml).

## Non-temporal locality hints

`RV5StageExtensions(~zihintntl: #true)` enables the four NTL hints and their
aliases when compressed instructions are selected. The generic extension option
defaults off; SimpleSoC, MiniSoC, and TiledSoC explicitly enable it. NTL retires
without draining or serializing the pipeline. Ordered WB retains its locality
selector for exactly the next
instruction: retirement consumes it even for a non-memory instruction, and
another retiring NTL replaces it. Rejected dispatch/replay preserves the hint
for the same target. Trap/interrupt entry clears it; speculative flushes do not.

Ordinary integer and FP loads/stores carry the selector in their accepted data
request. Other operations currently ignore it. MMU/PMA routing, lookup, and
retained L1D miss/refill context preserve the selector without changing
translation, permissions, ordering, or coherence. It expresses architectural
intent, not a cache policy. L1D currently interprets every non-default selector
as non-allocating on ordinary load misses, using its coherent transaction buffer;
hits and stores retain their existing behavior. See the
[L1D policy](dcache/README.md#miss-acquisition-and-replacement-flow) for ordering,
resident-line preservation, and outer-cache limits.

## Pipeline event tracing

The core carries metadata-only checkpoints named `core.s1.fetch`, `core.s2.decode`,
`core.s3.execute`, `core.s4.memory`, and `core.s5.wb`. Ordinary elaboration does not add
counters or DPI calls; the optional event compiler instruments a separate design.
The [SimpleSoC trace build](../../sims/README.md#export-simplesoc-events-to-perfetto)
includes these sites automatically. Stage-number prefixes keep their names in
pipeline order when sorted lexicographically.

Fetch records acceptance from the fetch queue into IF/ID and starts a new
lineage. Decode records issue after hazard and squash gating, not every stalled
cycle. Execute and Memory record surviving stage transfers; WB records arrival
at the scalar writeback stage. Inferred edges follow the real elastic IF/ID
controls and one-cycle always-capture registers. Repeated PCs have separate
occurrence identities; squashed tokens may have no later-stage descendant.

Each checkpoint captures only named `pc` and `instruction` fields (XLEN + 32
bits), not the complete stage bundle. Perfetto exposes `pc` as hexadecimal and
`instruction` as host-disassembled RISC-V text, using the core profile's ISA.
Raw instruction bits remain in the graph. These are already decompressed
pipeline instructions, so compressed instructions display their expanded form,
not the original `c.*` mnemonic.
WB is not retirement: replay, traps, maintenance/WRS holding, and deferred
load/multiply/divide/FP completion remain outside this first pipeline trace.
No dependency is inferred between the memory-boundary graph and fetch through
the cache/MMU, or across a replay's subsequent refetch.

### Private-cache outer traffic

The RV5Stage composition also annotates the L1I and L1D CHI interfaces, with
`icache.*` and `dcache.*` tracks. Each event is one accepted flit (`valid & ready`),
not an entire transaction or cache occupancy interval:

| Suffix | Transfer relative to the cache |
|---|---|
| `txreq` | Outgoing request, including miss, acquisition, and writeback requests |
| `rxrsp` | Incoming response, including completion and retry/credit messages |
| `rxdat` | Incoming data beat, including refill data |
| `txrsp` | Outgoing response, including CompAck and snoop responses |
| `txdat` | Outgoing data beat, including writeback and dirty snoop data |
| `rxsnp` | Incoming snoop |

Individual slices use the observed flit's CHI opcode name, such as `ReadClean`,
`CompData`, or `CompAck`; track names stay `icache.*` and `dcache.*`.
Captures include numeric CHI opcodes and transaction/source/target IDs where
present. REQ captures address, size, and retry/ack controls; RSP captures
DBID/group, response state/error, and credit type; DAT captures DBID/MECID,
DataID, response state/error, and byte enables, but **not the data payload**.
SNP captures its byte address (restoring the implicit three low zero bits),
source/transaction IDs, opcode, and return-to-source control. Opcode names come
from the channel's hardware enum declaration, not a separate host table. Unknown
encodings use hex slice names and retain their numeric opcode argument.

Each channel is an explicit root and terminal observation. These events do not
infer request-to-response ancestry through CHI transaction state, NoC/Home/LLC
logic, or the cache-to-pipeline path. Transaction IDs can be reused and have
channel-specific meaning; equality alone is not an event dependency. Uncached
RN-I traffic remains outside these private-cache checkpoints.

## Cache-block management

Zicbom operates on fixed 64-byte blocks. Decode checks current-privilege
CBIE/CBCFE permissions and resolves invalidate-to-flush conversion using the
[shared CMO policy](../../riscv/rtl/README.md#cache-block-permissions).
Translation independently uses effective data privilege, including MPRV, and
the management access class. The physical router checks the complete block.
The original unaligned rs1 address is preserved for faults; alignment is not
an exception for these instructions.

A CMO waits for older work in Decode and prevents younger instructions from
issuing. WB alone authorizes its memory request. Rejected dispatch replays;
accepted dispatch retains one retirement context outside the feed-forward
pipeline and is never reissued. Completion retires it, or raises a precise
store access fault on a CHI error. Younger effects and interrupt entry wait
until this context is resolved. CSR denial is illegal-instruction, and
translation denial is store-page-fault.

The D-cache sends a self-snooped CHI maintenance transaction even on a local
miss. Home owns coherent-system completion; the existing snoop path owns
local dirty-data transfer, cleaning, invalidation, and reservation changes.
Statically uncached physical regions cannot contain cached copies: after
permission checks and older traffic drain, they complete without device IO.
This does not add PMP, hypervisor support, or dynamic PMA reconfiguration.

## Microarchitecture

The five logical stages are regions of one [`RV5StageCore`](core.rhdl) circuit,
not module boundaries. Scalar tokens issue and reach WB in order, while
selected register-producing operations may complete later through explicit
scoreboards and a completion arbiter.

```mermaid
flowchart LR
    IMEM["ITLB + PMA<br/>L1I or uncached path"] --> IF

    subgraph scalar["Scalar pipeline — single issue, in-order commit"]
        IF["Fetch (IF)<br/>PC, correlation, redirects"]
        FQ["Fetch queue<br/>5 entries, non-pipe"]
        IFID["IF/ID<br/>elastic Pipe"]
        ID["Decode (ID)<br/>decode and hazards"]
        IDEX["ID/EX<br/>ValidPipeAlwaysCapture"]
        EX["Execute (EX)<br/>forwarding, branch, AGU"]
        EXMEM["EX/MEM<br/>ValidPipeAlwaysCapture"]
        MEM["Memory (MEM)<br/>prepared request, redirect, bypass"]
        MEMWB["MEM/WB<br/>ValidPipeAlwaysCapture"]
        WB["Writeback (WB)<br/>ordered commit"]

        IF --> FQ --> IFID --> ID --> IDEX --> EX --> EXMEM --> MEM --> MEMWB --> WB
    end

    WB -->|"load / store / AMO"| LSU["DTLB + PMA<br/>L1D or uncached path"]
    LSU -->|"integer load / AMO result"| COMPLETE["Deferred GPR<br/>completion arbiter"]

    WB -->|"issue at WB"| MUL["Multiplier"]
    WB -->|"issue at WB"| DIV["Divider"]
    MUL --> COMPLETE
    DIV --> COMPLETE

    WB -->|"FP compute issue"| FP["FP side pipeline<br/>scoreboard and execution"]
    WB -->|"best-effort prefetch"| PREFETCH["Registered VA → TLB probe + PMA<br/>Registered PA → L1I or L1D admission"]
    LSU -->|"FP load completion"| FP
    FP -->|"integer result"| COMPLETE
    FP --> FPR["FP register file"]

    WB -->|"ordinary result"| GPR["Integer register file"]
    COMPLETE --> GPR
    GPR -. "captured reads" .-> ID
    MEM -. "bypass" .-> EX
    WB -. "bypass" .-> EX

    SCORE["Integer and FP scoreboards"] -. "RAW / WAW stalls" .-> ID
    WB -->|"reserve FP compute destination"| SCORE
    WB -->|"reserve FP load destination"| SCORE
    WB -->|"reserve scalar deferred destination"| SCORE
    COMPLETE -->|"release GPR destination"| SCORE
    FP -->|"release FPR destination"| SCORE

    WB <--> CSR["CSR, trap, and interrupt state"]
    CSR -->|"trap / fence redirect"| IF
    MEM -->|"branch or fault squash"| IF
    WB -->|"memory replay refetch"| IF
```

### Stage contract

| Region | Output boundary | May hold? | Primary responsibility |
|---|---|---:|---|
| Fetch | Five-entry `Queue`, then IF/ID `Pipe` | Yes | Producer-owned PC generation, L1I request correlation, and redirect flushing |
| Decode | ID/EX `ValidPipeAlwaysCapture` | No | Structured decode, operand capture and bypass selection, serialization, RAW/WAW hazard checks, and local execution-resource reservation |
| Execute | EX/MEM `ValidPipeAlwaysCapture` | No | Registered-source forwarding, ALU, branch resolution, address generation, local synchronous-fault classification, FP operand preparation, and structural replay |
| Memory | MEM/WB `ValidPipeAlwaysCapture` | No | Prepared-request staging, branch recovery, early fault/replay squash, and bypass |
| Writeback | Ordered commit | At defined architectural waits | Memory/FP dispatch, translation and access faults, replay, register/CSR effects, traps, fences, and deferred reservations |

Within the pipeline, the nonbackpressured pipeline token uses `Valid` flow transforms
for fanout, filtering, and payload mapping. Ready-sensitive architectural
decisions remain explicit: in particular, an L1D request that is not ready
becomes a replay, so its request boundary must not turn readiness into EX/MEM
backpressure.

Fetch reserves up to four ordered, aligned instruction words in a flushable
ring, with separate request, response, and assembly pointers, so the physical
instruction hierarchy can preserve response order while
the pipelined L1I accepts and returns one hit per cycle. Redirects clear that
window and flush the MMU, instruction-router, L1I lookup, and buffered-response
state. A wrong-path refill may finish internally but cannot return an
instruction to Fetch; an in-flight uncached read is drained without publishing
its response.

Decode holds an instruction in IF/ID until its operands and locally reserved
execution resources are available. Once admitted, its ID/EX token advances on
the next edge. Decode captures register-file operands (including same-cycle
architectural writes) and chooses the MEM/WB sources that will be present in
Execute's next cycle. The youngest eligible writer wins; x0 never bypasses.
Loads, multiply/divide, and CSR results do not use the immediate-result bypass.
Execute selects only registered operands and registered producer data, never
live MEM/WB fault, replay, readiness, or redirect outcomes.

The RV5Stage pipeline and both prefetch boundaries use
`ValidPipeAlwaysCapture` to capture payload every cycle,
independently of token validity. Late faults, replay, and redirects cancel the
younger token and its side effects without changing its operands or enabling a
wide payload-register mux. Invalid payloads are unspecified and must be ignored.

Once Execute transfers an instruction into EX/MEM, no later scalar stage can
backpressure it. Execute prepares branch decisions, effective virtual addresses,
integer and FP operands, and locally classified faults. Memory stages those
values and performs branch recovery and integer bypass; it does not authorize
memory requests or FP execution.

WB is the boundary where an instruction becomes nonspeculative. Loads, stores,
LR/SC, AMOs, block zero, FP execution, and prefetch hints issue only from WB.
This includes loads because a translated address may select a side-effecting
device. CSR changes, fences, integer writes, and deferred destination reservations
also occur at WB or later. Early operand reads, instruction fetching, translation
bookkeeping, and autonomous cache/coherence activity are not architectural
instruction effects and remain independent.

A WB memory attempt performs translation and permission checks. A page/access
fault traps without authorizing a physical request; an unavailable resource
replays from the original PC without retirement or destination reservation.
Accepted requests execute exactly once and are never replayed. FP computation
uses the same accepted-or-replay rule. Deferred results can finish after younger
independent instructions, but all dispatch and scalar retirement remain ordered.
Late CHI bus-error handling remains the separate existing transport contract;
this boundary does not introduce a reorder buffer or precise late bus faults.

## Execution and completion

| Result class | Dispatch point | Completion path |
|---|---|---|
| Integer ALU, branch link, immediate, and ordinary CSR result | Scalar pipeline | Ordinary WB register-file port |
| Load or atomic result | Memory request and GPR reservation accepted at WB | L1D or uncached response to the deferred completion arbiter |
| Multiply or divide | Execution resource reserved in Decode; GPR reserved and request issued at WB | Deferred completion arbiter |
| FP result targeting an integer register | FP request and GPR reservation accepted at WB | FP completion to deferred completion arbiter |
| FP result targeting an FP register | FP request and FPR reservation accepted at WB | FP pipeline's internal FP register-file port |
| FP load | Memory request and FPR reservation accepted at WB | Memory response to FP pipeline's load port |

The fixed-priority deferred arbiter gives integer memory responses priority
because they cannot be backpressured. Multiplier, divider, and FP integer
responses remain stable until selected. Its output drives the second integer
register-file write port and clears the corresponding scoreboard entry. The
ordinary WB result uses the other write port. WAW gating prevents both ports
from targeting the same register in one cycle, and a WB-aligned cache hit can
set and clear a destination without an extra busy cycle.

[`fetch.rhdl`](fetch.rhdl) keeps a five-entry reserved word ring and a five-entry
flow-through queue of assembled instructions. The registered request PC follows
the predicted stream on request acceptance; the assembly PC follows the captured
prediction or advances by two or four bytes on instruction enqueue. Neither word consumption nor Decode readiness
selects the live request address, and returned buffer credit is registered.
With a ready consumer, the fifth word slot sustains one instruction per cycle on warm L1I hits even
when 32-bit instructions start at halfword offsets and span two fetched words.
The MMU admits S0 virtual reads into a two-entry non-flow-through request queue.
S1 translates its registered head while L1I resolves the preceding SRAM read;
S2 registers the selected word or refill context. A blocked S1 request remains
queued and locally reissues its virtual read, without core replay or duplicate
accepted physical requests. See the [MMU guide](mmu/README.md#request-flow).
With C enabled Fetch can reuse either halfword, assemble a
32-bit instruction that straddles adjacent words, and expand legal compressed
instructions before the ordinary decoder. It retains the original 16-bit word
for illegal-instruction trap values, reports second-word faults precisely, and
flushes retained, queued, or outstanding wrong-path data on redirects.

### Branch prediction

`RV5Stage` and `RV5StageCore` accept `~btb_entries` (default 16, zero disables
prediction). The fully associative BTB stores full instruction-PC tags, targets,
instruction lengths, and conditional/unconditional classification. Each entry
has a two-bit saturating counter; conditional branches predict taken in the upper
two states, and unconditional jumps predict taken on a hit. Invalid slots are
allocated first, then round-robin replacement is used. No return-address stack,
global history, or separate direction table is present.

Lookup runs alongside the current word request and chooses the earliest
predicted-taken branch at or after the request's starting halfword. An accepted
request captures its prediction in the word ring and selects the next request
PC. The target can be requested on the following cycle without a flush or
prediction-induced bubble. A 32-bit branch starting in the upper halfword first
requests its required continuation word, then the target. Cache/translation
misses, downstream stalls, and exhausted reservations still stall fetching.

Assembly retains instructions up to the predicted branch, discards trailing
halfwords, and continues through already requested target words. Predictions
belong to individual request occurrences, including repeated addresses in loops;
later BTB training cannot change a buffered instruction's predicted next PC.
Stale instruction lengths or cuts inside an instruction trigger local frontend
repair while preserving older assembled instructions.

EX computes the actual next PC; MEM recovers only when it differs from the
captured predicted next PC, with older exceptions and replay retaining priority.
Live nonfaulting/nonreplaying MEM instructions train the predictor. Taken misses
allocate weakly taken entries; conditional hits train on both outcomes. Resolved
nonbranches remove stale matching entries. Reset, `FENCE.I`, translation flushes,
and trap/return transitions invalidate the table; ordinary branch recovery does
not. Prediction is microarchitectural and does not change the ISA/UDB profile.

The optional [FP subsystem](fp/README.md) owns the FP register file, FPR
scoreboard, execution lanes, LSU bridges, and completion arbitration. FP
requests attempt dispatch at WB. A structurally rejected request replays without
retirement or reservation; an accepted request is nonspeculative and must
eventually complete. FP state updates accrue exception
flags and mark `mstatus.FS` dirty.

FP loads and stores share the scalar address generator, MMU, PMA checks, ordered
L1D, and uncached path. An FP load reserves its destination only when its WB
request is accepted. Decode probes an FP store's one-cycle register-file read
while the instruction is present, aligning its data with the instruction in EX;
a missing or stale response replays instead of holding EX. The cache data path
remains XLEN-wide: half and single stores occupy its low 16 or 32 bits, and half
and single loads are NaN-boxed into
the selected 32- or 64-bit FP register width. Exact precision metadata follows
a load through the MMU and cache response path.

## Control, hazards, and ordering

Decode stalls on scoreboard RAW and WAW hazards and on direct conflicts with
deferred instructions still crossing ID/EX or EX/MEM. Independent younger
instructions may proceed while a load, multiply, divide, or FP result remains
outstanding. D-cache responses are ordered, and a blocking miss prevents younger
memory requests from entering the cache even when non-memory work can pass it.

A data request never carries downstream readiness back through EX/MEM. If WB
cannot dispatch it because of a DTLB miss, walker ownership, cache pressure, or
an unavailable FP-load reservation, WB squashes younger work and refetches the
instruction without retiring it.
The initial DTLB-miss attempt is still sufficient to start the page-table walk;
subsequent refetches replay until the translation or other resource is ready.

The structured decoder selects the integer-only, RV32F, or RV64D base catalog
and optionally composes Zfhmin, Zfh, or Zfa at host elaboration. It emits component
control bundles through one hardware decode relation, without an
instruction-kind enum or parallel runtime decoders. Unused controls remain
synthesis don't-cares behind a separate valid bit.

Standard B and Zicond operations reuse the shared combinational
[`ALU`](../alu.rhdl). Zba, Zbb, Zbs, and Zicond add no second decoder, execution
unit, pipeline state, reservation, or scoreboard path. A operations use the
semantic memory machinery in [`memory.rhdl`](memory.rhdl) and return through the
same deferred path as loads.

CSR instructions return the old value and update state atomically at WB. System
instructions serialize in Decode and wait for older deferred work before
entering the pipeline. Execute-detected exceptions cross EX/MEM before Memory
squashes younger work. Data page and access faults are instead classified from
the registered virtual request at WB. CSR state records EPC, cause, and trap
value after older authorized memory and register-producing work has drained.
Eligible interrupts stop Fetch and Decode, drain the scalar pipeline and all
authorized memory/compute work, and enter the trap after the last retired
instruction. A legal `WFI`
retires at that same serialization boundary and then holds Fetch and Decode
until an individually enabled interrupt becomes pending. WFI wakeup ignores
global interrupt-enable and delegation state; an eligible interrupt enters its
handler with EPC equal to the instruction after `WFI`, while a globally masked
wake resumes that instruction directly. U-mode `WFI` and S-mode `WFI` with
`mstatus.TW` set raise an illegal-instruction exception.

`FENCE`, `FENCE.I`, and `SFENCE.VMA` share the serialization boundary. Decode
waits for older deferred completions and L1D quiescence, then prevents younger
instructions from entering Execute until the fence reaches WB. `FENCE.I` also
invalidates L1I and redirects Fetch to the fence's `pc + 4`; speculative fetch
flush and architectural cache invalidation remain distinct operations.

## System-facing composition

[`rv5stage.rhdl`](rv5stage.rhdl) wraps `RV5StageCore` with address translation,
physical-region routing, private caches, and CHI transaction boundaries:

```mermaid
flowchart LR
    PLATFORM["interrupts,<br/>hart_id, time_counter"] --> CORE["RV5StageCore"]
    IDENTITY["chi_identity<br/>RN NodeIDs"] --> L1I
    IDENTITY --> L1D
    IDENTITY --> UNCACHED

    CORE -->|"virtual instruction access"| MMU["MMU<br/>ITLB, DTLB, Sv39 walker"]
    CORE -->|"virtual data access"| MMU
    CORE -->|"privilege, mstatus, satp,<br/>translation flush"| MMU
    MMU -->|"early virtual index"| L1I
    MMU -->|"early virtual index"| L1D

    MMU -->|"physical instruction"| IROUTER["Instruction router<br/>PMA cacheability split"]
    IROUTER -->|"cacheable"| L1I["Private L1I"]
    IROUTER -->|"non-cacheable"| UNCACHED["One-outstanding<br/>shared uncached engine"]
    MMU -->|"physical data"| ROUTER["Memory router<br/>PMA and device split"]
    ROUTER -->|"cacheable"| L1D["Private L1D"]
    ROUTER -->|"non-cacheable"| IOMSHR["Data IO-MSHR<br/>retain through completion"]
    IOMSHR --> UNCACHED

    L1I <--> IMEM["imem<br/>CHI RN-F"]
    L1D <--> DMEM["dmem<br/>CHI RN-F"]
    UNCACHED <--> UMEM["umem<br/>CHI RN-I"]

```

`RV5StageCHIConfig` supplies the flit shape and a single list of physical
regions paired with CHI Homes. From that list it derives both the RISC-V
physical-memory map and `CHIHomeMap`, preventing permissions, cacheability, and
CHI routing from describing different address ranges. Cache transactions decode
their address once and retain the selected HN-F NodeID through retry, data, and
completion acknowledgement. The [RV5Stage CHI contract](chi/README.md) owns
endpoint configuration and shared transaction behavior.

`RV5StageCHIParams` contains host-only placement metadata for instruction and
data RN-F NodeIDs and the optional uncached RN-I NodeID. An occurrence receives
those values through `RV5StageCHIIdentity` hardware inputs, allowing one
specialized core definition to be stamped at multiple placements.

### Generator parameters

[`profile.rhm`](profile.rhm) defines the immutable `RVCoreProfile` host
model: XLEN, effective ISA extensions, MMU mode, and independent instruction
and data cache geometry. It validates supported combinations, derives the
canonical ISA extension list and `misa` value, and is the sole architectural
specialization input to `RV5Stage` and `RV5StageCore`.

| Parameter | Meaning |
|---|---|
| `profile.xlen` | Required `XLen.X32` or `XLen.X64` architectural width |
| `profile.extensions` | Floating-point, half-precision, Zfa, Zicbop, Zicboz, and compressed-extension selection; Zicbop and Zicboz default to disabled |
| `profile.mmu_mode` | `Bare` or, for RV64, `Sv39` translation behavior |
| `profile.cache_geometry` | Independent L1I and L1D set and way geometry |
| `~chi` | Required physical flit, address-region, and Home-routing policy |

All supported compressed-extension selections include Zca and permit two-byte
instruction alignment. A Zca-only integer core may advertise `misa.C`; once F
or D is present, `misa.C` is advertised only by the complete C composition
containing the corresponding Zcf or Zcd subset. Zcb is independently selected
and uses the same canonical 32-bit decode and execution paths. Zcmop is also
independently selected, expands its eight encodings to a canonical no-effect
instruction, and does not depend on Zimop. Neither extension affects `misa.C`.

Cache line size is fixed at 64 bytes and is not a generator parameter. Each way
contributes one XLEN-wide word to a data-array row, and a core lookup selects one
such word, so installation takes eight RV64 or sixteen RV32 SRAM writes. A
refill contains four, two, or one DAT packet for a supplied 128-, 256-, or
512-bit CHI data width, respectively; the default `CHIFlitParams()` width is 128
bits.

### UDB configuration

[`udb.rhm`](udb.rhm) projects an `RVCoreProfile` into a Unified Database fully
configured architecture. The profile selects XLEN, FP, compressed, and MMU
extensions. The projection adds the core's fixed architectural behavior,
including U/S/M privilege, direct-only `mtvec` and `stvec`, read-only `misa`,
no PMP or HPM counters, trapping misaligned accesses, exact-address-and-width
LR/SC reservations, and the implemented base counters. Physical address width
and PMA granularity remain explicit inputs because they are properties of the
core's integration rather than `RVCoreProfile`.

The projection declares `S` and `Sm` 1.12, matching the environment-configuration
CSRs and trap-return behavior. `mconfigptr` reads as zero (no configuration
structure), and RV32 `mstatush` and `menvcfgh` are fixed at zero with writes ignored. Sv39
profiles also declare `Svade`: unset PTE A/D bits cause page faults instead of
hardware page-table updates. Reference models derive these behaviors from UDB.

Generate the configuration for one checked-in SoC composition from the
repository root:

```sh
make riscv-udb-config RISCV_UDB_CONFIGURATION=simple-soc
```

The [SoC UDB configuration catalog](../../socs/README.md#risc-v-udb-configuration-catalog)
documents the available keys and output controls. Its RV5Stage entries read
the selected SoC's actual core profile and CHI request-address width and use a
PMA granularity of three, matching the shared platform's smallest eight-byte
PMA region.

The generated configuration is the architectural input to ACT4 test
selection. It is not the entire ACT target bundle: simulator macros, linker
and Sail configuration, and the execution command remain simulation-owned.

### Top-level ports

| Port | Contract |
|---|---|
| `chi_identity` | Placement-specific instruction RN-F, data RN-F, and uncached RN-I NodeIDs |
| `interrupts` | Controller-independent supervisor and machine software, timer, and external interrupt levels |
| `hart_id` | Platform hart identity exposed through `mhartid` |
| `time_counter` | Platform 64-bit time source exposed through `time` and RV32 `timeh` |
| `imem` | Instruction-cache CHI RN-F channels |
| `dmem` | Data-cache CHI RN-F channels |
| `umem` | Shared instruction/data uncached CHI RN-I channels |

`RV5Stage` and `RV5StageCore` require the host parameter `~reset_address`.
It must fit XLEN and be four-byte aligned, or two-byte aligned with compressed
instructions enabled; invalid addresses are rejected during elaboration.
After reset, instruction fetching starts automatically at that address.
BootROM software can wait for the host to prepare memory by polling a platform
register. The reset PC is a hardware specialization parameter, not the loaded
program's entry address; a BootROM can obtain that entry from a platform register.

Home Nodes, physical credited links, fabric topology, interrupt controllers,
and SoC policy remain outside RV5Stage.

## Memory hierarchy

Both L1 caches are non-aliasing virtually indexed, physically tagged (VIPT).
`RV5StageCacheConfig` rejects `sets * 64 > 4096`: the line offset and set index
must fit within a 4 KiB page, even for Bare profiles. Thus 64 sets is the maximum;
more ways increase total capacity without adding virtual index bits. Physical
tags retain every address bit above the set index, including page-offset bits
not consumed by smaller geometries. MiniSoC's 32-set, one-way caches remain 2 KiB.

RV64 supports Bare and Sv39 translation; RV32 remains Bare. Early virtual
lookups reach the SRAMs independently of translation and physical-region checks.
A permitted physical request is paired with the read at the clock edge; only
that resolved token can subsequently match physical tags or initiate a refill.
Unresolved or rejected reads have no completion or cache-state effect. Separate eight-entry fully
associative ITLB and DTLB instances retain PTE permissions and recheck current
privilege, `SUM`, and `MXR`. A single non-speculative walker services one miss at
a time through the shared physical data path after older cache or uncached work
drains; cacheable PTE reads then use L1D. A DTLB miss
starts that walk and returns an unaccepted request to the core, whose ordered
replay mechanism refetches the memory instruction until the lookup completes.
See the
[`MMU contract`](mmu/README.md) for translation, permission, and fault ownership.

L1I is a clean-only, one-hit-per-cycle instruction cache with flushable lookup
and response state. Executable non-cacheable regions bypass it as aligned
four-byte `ReadNoSnp` requests and never allocate a line. Such regions must be
read-idempotent; a typical BootROM PMA is readable, executable, non-cacheable,
non-atomic, non-device, and read-idempotent. L1D is a blocking write-back/
write-allocate cache supporting loads, stores, LR/SC, and AMOs. All
non-cacheable instruction and data requests arbitrate onto the same
one-outstanding RN-I engine. Non-cacheable data operations first enter a
single-entry [IO-MSHR](dcache/README.md#non-cacheable-data-io-mshr), independently
of an active instruction fetch. Its retained request takes priority at the
next engine arbitration and survives instruction flushes. The slot remains
occupied until completion; cached and uncached data demands cannot pass each
other, and fences include both queued and issued data operations. Instruction
traffic alone does not make the data path undrained.
Unmapped, denied, or non-cacheable atomic requests fault locally
instead of entering CHI.

With Zicboz enabled, `cbo.zero` zeros the entire naturally aligned 64-byte block
containing `rs1`. It uses the ordinary store-translation path, with no scalar
alignment requirement or register result. M-mode may always execute it;
S-mode requires `menvcfg.CBZE`, and U-mode requires both `menvcfg.CBZE` and
`senvcfg.CBZE`. These bit-7 fields reset to zero and are read-only zero when
the extension is disabled. Other environment-configuration fields remain zero.
The original virtual address is retained for store page/access faults.

The PMA router requires write permission and explicit block-zero support
over the complete block before accepting any side effect. Normal SoC RAM
supports block zero; ROM and devices do not. Cacheability is independent:
L1D zeros an exclusively owned line and retains it dirty, while uncached RAM
uses eight acknowledged 64-bit writes and produces one completion. The memory
path stays undrained throughout either operation. Fences wait for both
in-pipeline memory instructions and accepted memory work. Block zero does not
provide instruction-cache synchronization; modified code still needs FENCE.I.

With Zicbop enabled, `RV5StageCore` computes the virtual prefetch address in
Execute and, at WB, emits `Valid(CachePrefetchReq(xlen.width))`. This event has no
backpressure, response, or architectural-fault path. `RV5StageMmu` translates
Bare addresses or probes the operation-selected existing TLB entry; a miss,
permission denial, non-cacheable PMA, or intended-operation PMA denial drops
the event without walking or faulting. The accepted physical event is aligned
to its 64-byte line and routed to L1I for `PREFETCH.I` or L1D for
`PREFETCH.R/W`. The MMU registers hints both before translation and before cache
delivery; its [prefetch contract](mmu/README.md#best-effort-prefetch-probes)
defines the two-cycle latency and cancellation rules.

Demand requests always win each cache lookup port. An admitted L1I hint may
launch `ReadClean`; an admitted L1D read hint may launch `ReadClean`, while a
write hint may launch `ReadUnique` and retain the result as UniqueClean. Hits
produce no response, refills allocate without an architectural completion, and
L1D drops a hint that would require writing back a dirty victim.

The parent core owns only integration-level ordering. Array organization,
replacement, refill, dirty writeback, snoop behavior, DVM handling, and CHI
response stability are specified by the subsystem documents:

- [`icache/README.md`](icache/README.md) — instruction protocol and clean L1I
- [`dcache/README.md`](dcache/README.md) — data protocol and write-back L1D
- [`mmu/README.md`](mmu/README.md) — Sv39 translation and L1D walker arbitration
- [`chi/README.md`](chi/README.md) — shared CHI configuration, cache transaction
  engines, snoop handling, and uncached RN-I access

## Privileged and architectural state

[`csr.rhdl`](csr.rhdl) owns user, machine, and supervisor CSRs, current
privilege, trap entry, interrupt selection, and `MRET`/`SRET`. FP profiles add
the aliased `fflags`, `frm`, and `fcsr` views, `mstatus.FS` state, and derived
`SD`. The `csr_bank` declaration is the single source for recognized IDs, read
values, storage, aliases, WARL masks, and ordinary write dispatch.

The integer register file has 32 XLEN-wide registers with `x0` hardwired to
zero. An FP profile adds 32 raw FLEN-wide registers (32 bits for F, 64 bits for
D), but FP instructions and FP CSRs remain illegal while `mstatus.FS` is Off.
Accepted FP state changes mark FS Dirty, and `misa` reports the selected C, F,
and D features.

The core consumes controller-independent interrupt levels defined by
[`interrupt.rhdl`](interrupt.rhdl). CSR state combines them with writable
pending bits and applies enables, delegation, privilege, and architectural
priority. Reusable 64-bit `mcycle` and `minstret` state supplies Zicntr views;
`minstret` advances only when an instruction reaches WB without a synchronous
exception.

Both XLEN profiles implement the minimum Zihpm 2.0 contract: all 29
`mhpmcounter3`–`mhpmcounter31` and `mhpmevent3`–`mhpmevent31` slots
read zero and ignore machine-mode writes. Their read-only `hpmcounterN`
aliases also read zero in M mode; attempted writes trap. RV32 exposes the
corresponding machine/user counter high halves, while RV64 accesses to those
high-half addresses trap. HPM bits in `mcounteren` and `scounteren` are
hardwired zero, so S/U HPM reads trap even after software attempts to enable
them. There is no HPM storage, event selection, or counting datapath. The
[privileged architecture source](https://github.com/riscv/riscv-isa-manual/blob/main/src/priv/machine.adoc#hardware-performance-monitor)
permits these zero-valued counter/selector pairs.

## Implementation map

Source ownership and dependency enforcement moved to
[`DEVELOPING.md`](DEVELOPING.md#implementation-map). This heading remains for
existing links.

## Generated detailed diagrams

Contributor diagram generation moved to
[`DEVELOPING.md`](DEVELOPING.md#generated-detailed-diagrams).

## Verification

Contributor host, CIRCT, and Verilator workflows are documented in
[`DEVELOPING.md`](DEVELOPING.md#focused-validation). SoC-level architectural
and FESVR simulation belongs to the [simulation guide](../../sims/README.md).

## Pause hint

`RV5StageExtensions(~zihintpause: #true)` selects Zihintpause 2.0. The generic
default is disabled; SimpleSoC, MiniSoC, and TiledSoC enable it in their profiles.
ISA descriptions and UDB claims follow that selection; `misa` is unchanged.

PAUSE retires once at WB and starts a 16-cycle issue/fetch cooldown. It does not
drain older memory work, impose fence ordering, or wait for a reservation.
Deferred completions and coherence remain live. An enabled interrupt request
or CSR trap redirect cancels the cooldown. A squashed or faulting PAUSE cannot
start it. No clock gating, CSR state, or privilege restriction is added.
When disabled, its encoding retains the existing ordinary FENCE behavior.

## Reservation waiting

`RV5StageExtensions(~zawrs: #true)` enables Zawrs through the core profile,
including decoder selection, ISA descriptions, and the UDB extension claim.
The generic extension default remains disabled; SimpleSoC, MiniSoC, and
TiledSoC explicitly enable it. Zawrs adds no single-letter `misa` bit.

WRS serializes behind older authorized work and is accepted only at WB. A
pending instruction retains its retirement context outside the feed-forward
pipeline, blocks younger issue, and pauses Fetch without blocking coherence
service. The data interface's `reservation_valid` is the cache-owned LR/SC
level, forwarded unchanged through the memory router and MMU. An invalid
reservation or a locally enabled pending interrupt completes the wait,
regardless of global interrupt enable. Level observation covers invalidation
before entry as well as during waiting; completing WRS does not clear LR state.

`WRS.STO` completes after at most 4,096 pending cycles. `WRS.NTO` has no normal
timeout, but below M-mode with `mstatus.TW=1` the same bound raises an illegal
instruction exception with the original PC and instruction. Wake wins over
timeout on the same cycle. Both instructions are legal in U-mode, and TW does
not restrict STO. Successful completion retires exactly once; a timeout trap
does not retire. An interrupt taken after a successful wake records the
successor PC. Clock gating and hypervisor modes are not implemented.

## Deliberate limits

- RV32D and RV64F-only core specializations are rejected.
- PMP, programmable HPM counters, vectored trap mode, and platform interrupt
  controllers remain outside this slice.
- `WFI` quiesces instruction issue but does not gate the core clock; physical
  clock gating and always-on wake distribution remain platform policy.
- Sv48/Sv57, nonzero ASIDs, hardware A/D updates, PBMT, NAPOT, multi-hart
  shootdown, and speculative page-table walks are not implemented.
- `SFENCE.VMA` and `satp` writes conservatively flush both TLBs completely.
- Zicbop translation is TLB-hit-only and never launches a page-table walk;
  hints may be dropped under translation, PMA, lookup, refill, snoop, response
  capacity, or dirty-victim pressure.
- The private caches do not implement hit-under-miss or autonomous prefetching;
  detailed cache-specific limits are maintained in their owning READMEs.
