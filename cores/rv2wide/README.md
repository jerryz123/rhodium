<!-- Documents RV2Wide's dual-issue execution, nonblocking memory, and retirement interfaces. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV2Wide

RV2Wide is an in-order dual-issue processor under construction. The current
`RV2WideCore()` implements the RR-through-WB integer execution slice, not a
complete hart or the eventual seven-stage frontend/core. It executes RV64I
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

Provide `instructions: Decoupled(RV2WidePacket())`. Each packet has a `count`
of one or two and that many valid `entries`, oldest first. Entries contain a
64-bit PC and a 32-bit instruction. The four-entry issue window retains
unconsumed instructions and coalesces adjacent packets after partial issue.
The source must follow redirects and supply instructions in program order;
the slice does not fetch instructions itself.

## Execution and ordering

Current implementation:

```text
instruction packets -> four-entry window -> RR -> EX -> MEM -> WB
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

Branches resolve in EX but redirect at WB. A taken older
branch suppresses the younger slot; a taken younger branch preserves the older
retirement. Every redirect clears younger pipeline and window work and rejects
instruction admission on that edge. A not-taken branch does not redirect.

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
WB chooses the oldest stop, allowing a preceding successful instruction to
retire exactly once. `redirect: Valid(RV2WideRedirect())` reports that stop:

- Continue: a successful taken branch; `target` is the resolved destination.
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

There is no fetch frontend, C expansion, attached cache/MMU, CSR/trap-state unit,
interrupt handling, M/A/B decode, or SoC binding yet. Naturally misaligned
loads/stores fault before lookup; split accesses are not implemented.
This execution slice makes no full RV64I or RV64IMACB architectural profile
claim. It is not selectable through the SoC configuration resolver.

The intended initial integration target is the existing lean RV64IMACB preset,
with all of that preset's system properties, once implemented. Shared ISA
descriptors remain in `riscv/`; named-core execution policy remains here.
