<!-- Documents reusable branch-target, direction-table, and return-address prediction contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Branch prediction

`bpred/` provides shared `Btb`, `Bht`, and `Ras` components. RV5Stage uses BTB/RAS;
RV2Wide additionally uses the direction table. They own predictor state, not fetch sequencing, instruction assembly, redirect
priority, or retirement. See [DEVELOPING.md](DEVELOPING.md) for maintenance and
[the core catalog](../README.md) for consumers.

## Get started

```rhdl
import:
  lib("cores/bpred/btb.rhdl") open
  lib("cores/bpred/ras.rhdl") open
  lib("cores/bpred/protocol.rhdl") open
  lib("riscv/isa/xlen.rhm").XLen

inst btb(Btb(XLen.X64))
inst ras(Ras(XLen.X64))
```

The caller drives the BTB cursor and event inputs, uses `prediction`, and
selects the RAS head for return predictions. These components do not connect
themselves to a fetch pipeline. Shared setup is in the
[repository quick start](../../README.md#quick-start).

## Branch-target buffer

`Btb(xlen, entry_count = 32, match_bits = 14, page_count = 8, ~fetch_bytes: 4)` has a
combinational `cursor` input and `BranchPrediction(xlen)` output. Lookup selects
the earliest predicted-taken instruction at or after the cursor **within its
aligned fetch block**. `fetch_bytes` supports four (the default) or eight bytes.
Halfword-aligned entry PCs support compressed code;
the caller validates instruction boundaries and predicted lengths against
fetched data. `prediction.valid` is false on a miss or full invalidation.

Entries store low PC/target bits and indices into shared upper-address tags.
Matching still checks the complete address. Page replacement invalidates every
entry using that page as either a source or target. Empty entry slots are used
before round-robin replacement. Conditional branches have two-bit saturating
counters; the upper two states predict taken. Unconditional entries predict
taken whenever they match.

The state-update interfaces are:

- `update: Valid(BranchUpdate(xlen))` trains a resolved outcome. `branch`
  identifies a control-flow instruction, including unconditional jumps;
  a false value removes a matching stale entry. A taken control-flow miss
  allocates a weakly-taken entry, while a not-taken miss does not allocate.
  Existing counters update on both taken and not-taken outcomes.
- `discover: Valid(BranchPrediction(xlen))` installs a valid early-discovered
  unconditional jump or return. Resolved `update` takes priority over discovery.
- `invalidate: Valid(Bits(xlen.width))` removes an exact instruction-PC match
  and suppresses simultaneous training/discovery writes.
- `invalidate_all: Pulse()` clears entries, page tags, and replacement state.
  Reset does the same. The caller chooses when architectural context or code
  changes require invalidation.

`BranchPrediction` carries `valid`, instruction `pc`, `target`, `compressed`,
and `ras_action`. `BranchUpdate` adds conditional/taken classification and
actual/predicted RAS actions plus the return address. The caller qualifies
updates against its own squash and resolution rules. Invalid prediction
payload fields must not be observed.

`entry_count` is positive. Select `DisabledBtb(xlen)` instead to disable prediction
with the same ports and no predictor state. `match_bits` must include the fetch offset and
less than XLEN; `page_count` must be at least two. BTB matching has no address-space tag.

## Direction table

`Bht(row_bits = 10)` stores four two-bit saturating counters per row. Its
default 1,024 rows provide 4,096 counters (1 KiB of counter data), plus row-valid
bits. `row` selects four halfword banks combinationally; `prefix_row` independently
reads bank three for an unfinished byte-six instruction. Cold counters are
weakly not-taken. Asynchronous reads do not guarantee SRAM mapping.

`update: Valid(BhtUpdate(row_bits))` supplies a saved index and actual `taken`
outcome. Index bits 1:0 select the bank; upper bits select the row. Training
saturates the current counter, so consecutive aliased updates are not lost.
Reset and `clear: Pulse()` invalidate rows; the next write initializes the
remaining banks in that row. Clear overrides training and exposes cold lookup
values immediately. `DisabledBht` retains these ports without table state.

Callers own PC/history hashing, speculative history, instruction boundaries,
training authorization, and recovery. `BhtPrediction(row_bits)` carries a
conditional-valid bit, saved index, pre-instruction history, and predicted
outcome; a not-taken prediction is still valid metadata.

## Return-address stack

`Ras(xlen, entry_count = 6)` maintains separate speculative and resolved bounded
stacks. `head_valid` and `head` expose the speculative top combinationally.
Overflow wraps and replaces the oldest address; underflow is a no-op.
`entry_count` is positive; `DisabledRas(xlen)` exposes the same ports with an
always-invalid head and no stack state. Core configurations may still use zero
entries to select these disabled circuits at instantiation.

- `speculate: Valid(RasUpdate(xlen))` applies an accepted speculative action.
  The caller must emit it exactly once, not repeatedly while fetch stalls.
- `resolve: Valid(RasResolution(xlen))` applies the actual action to resolved
  state. A mismatch with `predicted_action` restores speculative state to the
  updated resolved state.
- `restore: Pulse()` restores resolved state after recovery. If simultaneous
  with resolution, restoration includes that resolution's actual action.
- `clear: Pulse()` and reset empty both stacks. Clear has highest priority;
  restoration or action reconciliation takes priority over speculation.

`RasAction` is `None`, `Push`, `Pop`, or `PopPush`; `PopPush` replaces the
current top, making an empty stack nonempty. `RasUpdate` carries `action` and
`return_address`; `RasResolution` carries `actual` and `predicted_action`.

`riscv_ras_action(instruction)` classifies canonical JAL/JALR `x1`/`x5`
call/return hints, including coroutine behavior.
`riscv_compressed_ras_action(xlen, instruction)` classifies raw compressed
hints, including RV32-only C.JAL. These helpers classify hints rather than
validate instruction legality. The caller supplies the length-correct return
address and decides whether a stack head overrides a BTB target.

## Integration boundary

The caller owns when to redirect, which instructions remain in a fetched
packet, cross-word continuation, prediction verification, context invalidation,
and which resolved actions survive recovery. No predictor input is itself
authorization to retire an instruction.

RV5Stage's current integration is documented in its
[branch-prediction contract](../rv5stage/README.md#branch-prediction) and
[fetch guide](../rv5stage/fetch/README.md). RV2Wide uses an eight-byte lookup
and WB-qualified training; see its [fetch contract](../rv2wide/README.md#branch-prediction).
