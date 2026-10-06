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
its accepted load owners later write through the shared younger-slot port.
Redirect qualification gates new transfers as well as flushing pipe state.

Reuse ALU, branch, load/store shaping, and shared scoreboard components and the `cores/riscv/` instruction
relations. Never import RV5Stage or another named core. Do not create an
instruction-kind enum followed by a second runtime control decoder.
Core control rows join exact canonical instruction patterns using
`component_output`, following RV5Stage's composition pattern. Unobserved
payload controls stay don't-cares behind cared enables/source-use bits.

## Implementation map

| Owner | Responsibility |
|---|---|
| `decode/operand-ctrl.rhdl` | Source-use bits, ALU operands, canonical immediate selection |
| `decode/mem-ctrl.rhdl` | Load/store enable, direction, width, signedness, inactive care masks |
| `decode/system-ctrl.rhdl` | Architectural CSR operation/source and trap/return/wait columns |
| `decode/core-ctrl.rhdl` | Selected instruction domain, writeback column, one combined relation |
| `bundles.rhdl` | Instruction, lookup/admission/response, stage, and retirement contracts |
| `issue-window.rhdl` | Sole eight-entry compacting instruction buffer, free-entry count, prefix consumption |
| `frontend.rhdl` | Reserved-capacity block fetch, physical permissions, local replay, and fault packet assembly |
| `core.rhdl` | RR/EX/MEM/WB, forwarding, shared component/CSR instances, register state, precise traps |
| `load-response.rhdl` | Four accepted contexts, atomic response/owner joining, shared load extraction |
| `cache.rhdl` | Physical RAM permission checks and raw-beat adaptation to the shared L1D |
| `rv2wide.rhdl` | Frontend/core/shared L1I/L1D composition, distinct CHI identities, start/halt boundary |
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

Keep accepted ownership independent of speculative flush. The completion FIFO
joins ordered responses through `zip_flow`, normalizes returned data with
`LoadGen`, and reserves the younger RR slot for responses requiring a GPR write.
An unflushable three-stage Valid pipe aligns those responses with the vacant WB
slot. Mux the completion and younger instruction before the second register-file
write connection; do not add a third write port or backpressure WB. The
completion-pipeline occupancy participates in precise fault drain.
Pre-WB producer checks bridge the interval before the scoreboard is set.
RAW interlocks select the youngest older producer; WAW interlocks cover all
older deferred producers. A completion may forward to RR on its write edge.

A MEM branch flush clears the instruction buffer, RR, and EX, including EX
lookup admission, while preserving its own MEM-to-WB transfer and older WB work.
An older taken branch also suppresses its same-group younger MEM transfer.
Branch selection observes both lanes' resolved MEM outcomes. WB faults/replays
and retained faults override MEM recovery; branches never redirect twice.
An older same-group memory instruction may reject WB authorization after MEM
branch recovery; its WB redirect must override that speculative target and
suppress the younger branch's retirement. No branch prediction is involved.

A pending fault squashes younger pipeline/window work immediately, but its
CSR trap command and public redirect wait for both the owner FIFO and memory service to drain.
Same-group older acceptance participates in that drain decision. Branch and
replay redirects do not cancel or wait for successful accepted work.
Reset is coordinated with the memory service; it ends the entire response epoch.

The frontend pairs one accepted S0 virtual index with S1 physical permission
resolution and an S2 block outcome. Both lookup contexts use flushable Flow
Valid pipes. Reserve two entries for each admitted lookup in the core's sole
eight-entry instruction buffer, using its registered free-entry count. S2
assembly feeds that buffer directly, with no intervening packet queue. Never
depend on current issue readiness to accept a cache result. Replay kills younger
lookup stages only; MEM/WB recovery also clears the instruction buffer. Start is
quiescent-only and the buffer is already empty. Accepted refills belong to L1I
and remain live across speculative cancellation. Fetch faults carry explicit
cause/address through the ordinary instruction token and suppress decode
hazards, memory requests, branch effects, and GPR writes.
The core's immediate `fetch_flush` pulse also stops fetch during a retained WB
fault, before its drained public redirect. A simultaneous redirect supplies the
new cursor; otherwise fetch remains stopped. Buffer credits alone cannot express
this cancellation because a cleared buffer can still be rejecting admission.

Keep `start` reset-only, with the data service drained. It is not an asynchronous
core flush input. The integrated top follows CSR trap/return redirects as well
as branches/replays; WFI has a separate sleep indication.

## System operations and precise boundaries

RR serializes system instructions after older pipeline and accepted memory work
drain and never admits a system instruction as the younger slot. CSR results
are unavailable to speculative forwarding; WB writes the shared bank's result.
Younger ordinary instructions may issue, but every WB system command flushes
their effects and refetches from the architectural successor or trap/return PC.
This cut gives WB CSR recovery priority over a younger MEM branch without
making feed-forward stage registers elastic.

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

## Validation

Run the focused production-core fixture:

```sh
FIXTURE=rv2wide-core bash tools/testing/circt/run.sh --simulate-only
FIXTURE=rv2wide-cache bash tools/testing/circt/run.sh --simulate-only
FIXTURE=rv2wide-fetch bash tools/testing/circt/run.sh --simulate-only
make check-boundaries
```

The fixture belongs to `cores-execution-datapath`. It checks sustained dual
retirement, packet coalescing, RAW/WAW and x0, youngest-producer forwarding,
RV64/word ALU operations, seeded dependency-heavy arithmetic, signed/unsigned
branches and jumps in either slot, JALR masking, misalignment/illegal faults,
MEM qualification, replay/restart, older-fault priority, and reset cancellation.
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
It compares architectural retirement against an independent sequential model,
not internal register names. External qualification is matched to public MEM
PCs; no test-only RTL switches or hierarchical state mutations are used.

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

The fixture runner invokes the repository-managed Racket wrapper. Run host
checks through `tools/run-racket-tests.sh` and other elaboration through
`tools/run-racket.sh`; do not bypass the managed compiled root. After changing
fixture ownership, confirm `--group cores-execution-datapath --list-fixtures`
includes `rv2wide-core`. No new SoC or software CI configuration belongs to this
initial execution-slice milestone.
