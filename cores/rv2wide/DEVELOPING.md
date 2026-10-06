<!-- Defines RV2Wide stage, memory-ownership, decode-composition, and validation rules. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RV2Wide

Read [README.md](README.md) for the current public contract. This guide owns
the integer slice, its age/order rules, and its behavioral fixture. Follow
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
| `decode/core-ctrl.rhdl` | Selected instruction domain, writeback column, one combined relation |
| `bundles.rhdl` | Instruction, lookup/admission/response, stage, and retirement contracts |
| `issue-window.rhdl` | Four-entry two-wide packet storage and prefix consumption |
| `core.rhdl` | RR/EX/MEM/WB, forwarding, shared component instances, register state, precise stops |
| `load-response.rhdl` | Four accepted contexts, atomic response/owner joining, shared load extraction |
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

A pending fault squashes younger pipeline/window work immediately, but its
public redirect waits for both the owner FIFO and memory service to drain.
Same-group older acceptance participates in that drain decision. Branch and
replay redirects do not cancel or wait for successful accepted work.
Reset is coordinated with the memory service; it ends the entire response epoch.

## Validation

Run the focused production-core fixture:

```sh
FIXTURE=rv2wide-core bash tools/testing/circt/run.sh --simulate-only
make check-boundaries
```

The fixture belongs to `cores-execution-datapath`. It checks sustained dual
retirement, packet coalescing, RAW/WAW and x0, youngest-producer forwarding,
RV64/word ALU operations, seeded dependency-heavy arithmetic, signed/unsigned
branches and jumps in either slot, JALR masking, misalignment/illegal faults,
MEM qualification, replay/restart, older-fault priority, and reset cancellation.
The same fixture drives the production LSU boundary with a controlled cache
service. It checks hit throughput, every natural byte lane and load extension,
store masks, four outstanding owners, hit-under-miss, RAW/WAW scoreboarding,
completion/older-slot simultaneous writes and younger-slot reservation, capacity and admission replay, store-commit
rejection, lookup/admission faults, and accepted-work drain across precise stops.
It compares architectural retirement against an independent sequential model,
not internal register names. External qualification is matched to public MEM
PCs; no test-only RTL switches or hierarchical state mutations are used.

The fixture runner invokes the repository-managed Racket wrapper. Run host
checks through `tools/run-racket-tests.sh` and other elaboration through
`tools/run-racket.sh`; do not bypass the managed compiled root. After changing
fixture ownership, confirm `--group cores-execution-datapath --list-fixtures`
includes `rv2wide-core`. No new SoC or software CI configuration belongs to this
initial execution-slice milestone.
