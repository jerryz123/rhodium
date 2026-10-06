<!-- Documents RV2Wide's physical fetch frontend, dual-issue execution, and precise retirement interfaces. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV2Wide

RV2Wide is an in-order dual-issue processor under construction. `RV2Wide`
fetches instructions through a shared L1I and executes through the shared L1D;
it is physical-addressed, without privileged state. `RV2WideCore()` remains the
independently usable RR-through-WB execution slice. It executes RV64I
integer arithmetic, word arithmetic, LUI/AUIPC, branches, JAL/JALR, and naturally
aligned scalar loads/stores through a pipelined memory-service boundary.
See [DEVELOPING.md](DEVELOPING.md) for ownership and validation.

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
Connect separate `instruction_chi` RN-I and `data_chi` RN-F endpoints and
distinct `instruction_node_id` / `data_node_id` inputs. Pulse
`start: Valid(Bits(64))` with the initial physical PC after reset. `halted`
is initially true and becomes true again after a precise fault is reported;
only then may another start pulse resume execution. Restart does not reset GPRs
or cache residency. A reset must reset the external memory-service epoch too.

Retirement, completion, redirect, and issue-count outputs remain observable.
Successful branches and memory replays redirect fetch internally. Faults are
reported at WB and halt fetching; there is no implicit trap handler.
`cores/rv2wide/cache.rhdl` owns the standalone `RV2WideL1D` adapter.

When using `RV2WideCore()` directly, provide `instructions: Decoupled(RV2WidePacket())`. Each packet has a `count`
of one or two and that many valid `entries`, oldest first. Entries contain a
64-bit PC, a 32-bit instruction, and optional fetch-fault cause/address. Clear
`fault.valid` for successful fetches; instruction bits are ignored for faults.
The eight-entry instruction buffer retains
unconsumed instructions and coalesces adjacent packets after partial issue.
The source must follow redirects and supply instructions in program order;
the standalone slice does not fetch instructions itself. `fetch_flush: Pulse()`
immediately cancels younger fetch work, even while a fault is waiting for older
memory work to drain. Stop fetching on that pulse until a redirect or explicit
restart supplies the next PC; a simultaneous redirect takes priority.

## Instruction fetch

The current frontend/caches implement this pipeline:

```text
IF1: PC / L1I array admission -> IF2: physical permission / tag resolution
  -> ID: 64-bit result / ordered packet assembly -> issue window -> RR -> EX -> MEM -> WB
```

Each aligned eight-byte block supplies two 32-bit instructions, low address
first. A restart at offset four supplies only the upper instruction; the issue
window combines it with the next block. Blocks never cross a cache line or page.
Consecutive hits can sustain two instructions per cycle. The frontend reserves
two instruction entries per in-flight lookup in the core's sole compacting
instruction buffer before issuing SRAM reads, so a blocked core cannot lose
responses. There is no separate fetch-packet queue. The execution slice exposes
`instruction_capacity: Bits(4)`, its registered free-entry count, for this
reservation accounting; it does not depend on the current issue decision.

L1I misses restart the failed fetch attempt and discard younger attempts,
preserving older buffered instructions. A MEM branch or WB redirect clears speculative fetch
and issue storage, but accepted CHI refills continue draining. Wrong-path
errors cannot become architectural faults. A failed block produces one fault
token at the first requested instruction PC, not an illegal instruction derived
from undefined data. WB preserves age ordering and drains older accepted data
transactions before reporting the fault.

Physical permission and address-width checks happen before cache resolution.
Only executable, instruction-cacheable, idempotent memory is fetched: coherent
RAM uses `ReadOnce`, immutable ROM uses `ReadNoSnp`. Other regions report an
instruction access fault without CHI traffic. Non-four-byte-aligned starts
report instruction-address-misaligned faults. No predictor or C assembly is
included; fetch always advances sequentially until a resolved redirect.

## Execution and ordering

Current implementation:

```text
instruction packets -> eight-entry buffer -> RR -> EX -> MEM -> WB
                                            ^     |      |     |
                                            +-----+------+-----+ forwarding
```

RR consumes zero, one, or two instructions. Independent ALU operations can
issue and retire two per cycle. Same-group RAW/WAW conflicts split the pair;
the younger instruction stays at the head for the next cycle. One branch
resolver serves either age slot, and two branches cannot issue together.
The shared `cores/alu.rhdl` and `cores/branch-resolver.rhdl` own execution.
Each slot uses one composed structured decoder, with the shared component
relations joined to RV2Wide operand/writeback controls.

EX, MEM, and WB use feed-forward Flow `ValidPipe` registers. Forwarding searches
both lanes, preferring the youngest older producer. A matching unavailable
result blocks instead of exposing stale register data. There are four GPR reads
and two GPR write ports. A shared scoreboard
reserves accepted slow-load destinations at WB. RAW and WAW interlocks cover
both older pipeline lanes and outstanding destinations; completion forwards
on its write edge. Independent work continues while loads are outstanding.
Register state resets to zero; x0 ignores writes but does not suppress memory
accesses or their faults. Same-cycle writes have distinct nonzero destinations.

`retired[2]: Valid(RV2WideRetirement())` reports the successful ordered prefix
at WB, including PC, encoding, destination, write enable, and value. `issued`
and `retired_count` report counts of zero, one, or two for the current edge.
Outputs have no backpressure. `deferred` means the memory transaction was
accepted at WB and its response will arrive on `completed`, rather than being
reissued or retired again. For a deferred load, `write` describes the eventual
GPR write and retirement `data` is unspecified. `completed` preserves the
instruction identity and reports the returned value; store acknowledgements
have `write` false. Non-writing data is unspecified on either interface.

Branches resolve in EX and redirect from MEM. A taken older branch suppresses
the younger slot; a taken younger branch preserves the older peer. Both the
branch and any older peer still retire at WB. MEM recovery clears younger EX,
RR, and fetch work, not older WB work or accepted memory transactions. WB faults
and replays take priority over simultaneous MEM recovery. An older same-group
memory operation can still fail authorization at WB on the following cycle;
that recovery overrides the earlier branch target and prevents branch retirement.
Every redirect rejects instruction admission on that edge. A not-taken branch
does not redirect, and a redirected branch does not redirect again at WB.

## Pipelined memory and deferred completion

One load/store may issue per group, in either age slot. The memory service has
the same lookup-versus-authorization split as RV5Stage:

```text
EX -- Valid lookup --> memory service -- fixed MEM result --> WB
                                                            |
                        hit-store commit / slow admission <--+
                                                            |
              accepted owner FIFO <-- WB acceptance --> scoreboard set
                      |                                      |
ordered slow response +--> LoadGen --> reserve younger RR slot
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

`memory: RV2WideMemory()` accepts WB `Decoupled` requests. Admission is
non-speculative and resolves synchronous exceptions **before acceptance**:
`fault.valid` supplies a Fault resolution, including its precise address, and
the service must deassert request readiness. Otherwise a transfer irrevocably
owns one successful response. Unaccepted offers are withdrawn and replayed.
Both loads and stores return one ordered raw beat; store response data is ignored.
The service must preserve architectural memory ordering and prevent stale hit
results from bypassing older overlapping writes, using forwarding or replay.
Device accesses must use Slow, never a speculative hit.

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
slot. The scoreboard remains set until the actual WB write, and fault drain
includes completions already in this pipeline.

Fault entry waits for accepted memory work to drain. The core immediately
squashes younger instructions and retains the fault report, then emits it after
all older effects and deferred GPR writes complete. This also covers an accepted
older load paired with a faulting younger instruction. No global memory-busy
interlock serializes independent ALUs or cache hits.

The boundary requires admission-certified aligned transactions, not an arbitrary
bus that may raise a late synchronous exception. A future fault-capable service
such as split-page accesses must retain retirement ownership until its final
fault decision; it cannot reuse the successful deferred-response contract.

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

- Continue: a taken branch at MEM; `target` is the resolved destination. This
  is speculative recovery, not a retirement notification.
- Replay: restart at `pc`; the instruction has not committed.
- Fault: `pc`, cause, and fault value are reported; `target` is the faulting PC,
  **not a privileged trap vector**. The surrounding controller must handle it.

With no C support yet, instruction PCs and taken targets require four-byte
alignment. JALR clears bit zero before the alignment check. A misaligned target
faults without writing its link register. Unmatched encodings report an illegal
instruction fault with the original encoding. Reset clears queued/pipelined
work, completion ownership, scoreboards, and architectural registers. It is a
coordinated epoch boundary: the memory service must reset with the core and
must not return pre-reset responses afterward.

## Deliberate limits

There is no C expansion, MMU, CSR/trap-state unit,
interrupt handling, M/A/B decode, or SoC binding yet. Naturally misaligned
loads/stores fault before lookup; split accesses are not implemented.
This execution slice makes no full RV64I or RV64IMACB architectural profile
claim. It is not selectable through the SoC configuration resolver.

The intended initial integration target is the existing lean RV64IMACB preset,
with all of that preset's system properties, once implemented. Shared ISA
descriptors remain in `riscv/`; named-core execution policy remains here.

The data-cache adapter uses physical addresses and checks its CHI physical map before
lookup or authorized admission. Only coherent, cacheable, idempotent RAM is
supported; unmapped, non-cacheable, or disallowed accesses report access faults
without CHI traffic. There is no MMIO fallback. The adapter requests aligned
full-width beats while preserving positioned byte masks; the core retains
load extension and completion ownership. Accepted traffic drains across
redirects, and the two-port writeback policy is unchanged.
