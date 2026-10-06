<!-- Documents the executable RV2Wide integer issue/retirement slice and its integration boundary. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV2Wide

RV2Wide is an in-order dual-issue processor under construction. The current
`RV2WideCore()` implements the RR-through-WB integer execution slice, not a
complete hart or the eventual seven-stage frontend/core. It executes RV64I
integer arithmetic, word arithmetic, LUI/AUIPC, branches, and JAL/JALR.
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
result blocks instead of exposing stale register data. This slice has four
GPR reads and two WB writes; deferred execution and its additional completion
port/scoreboard are not yet implemented. Register state resets to zero and
x0 ignores writes. The two same-cycle writes have distinct nonzero destinations.

`retired[2]: Valid(RV2WideRetirement())` reports the successful ordered prefix
at WB, including PC, encoding, destination, write enable, and value. `issued`
and `retired_count` report counts of zero, one, or two for the current edge.
Outputs have no backpressure. Non-writing retirement payload data is unspecified.

Branches resolve in EX but redirect at WB in this first slice. A taken older
branch suppresses the younger slot; a taken younger branch preserves the older
retirement. Every redirect clears younger pipeline and window work and rejects
instruction admission on that edge. A not-taken branch does not redirect.

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
work and architectural registers without later retirement of discarded work.

## Deliberate limits

There is no fetch frontend, C expansion, LSU, cache, MMU, CSR/trap-state unit,
interrupt handling, M/A/B decode, deferred completion, or SoC binding yet.
This execution slice makes no full RV64I or RV64IMACB architectural profile
claim. It is not selectable through the SoC configuration resolver.

The intended initial integration target is the existing lean RV64IMACB preset,
with all of that preset's system properties, once implemented. Shared ISA
descriptors remain in `riscv/`; named-core execution policy remains here.
