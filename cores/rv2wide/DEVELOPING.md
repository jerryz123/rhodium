<!-- Defines RV2Wide slice ownership, structured decode composition, and focused behavioral validation. -->
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
older blocked instruction. WB is the only GPR-write and retirement authority.
Redirect qualification gates new transfers as well as flushing pipe state.

Reuse ALU and branch physical components and their `cores/riscv/` instruction
relations. Never import RV5Stage or another named core. Do not create an
instruction-kind enum followed by a second runtime control decoder.
Core control rows join exact canonical instruction patterns using
`component_output`, following RV5Stage's composition pattern. Unobserved
payload controls stay don't-cares behind cared enables/source-use bits.

## Implementation map

| Owner | Responsibility |
|---|---|
| `decode/operand-ctrl.rhdl` | Source-use bits, ALU operands, canonical immediate selection |
| `decode/core-ctrl.rhdl` | Selected instruction domain, writeback column, one combined relation |
| `bundles.rhdl` | Instruction, packet, stage, retirement, and resolution contracts |
| `issue-window.rhdl` | Four-entry two-wide packet storage and prefix consumption |
| `core.rhdl` | RR/EX/MEM/WB, forwarding, shared component instances, register state, precise stops |
| `tests/circt/` | Production-core emitter and independent sequential-result/ordering oracle |

## Change workflow

1. Update the public contract before broadening the implemented execution slice.
2. Add canonical instruction descriptors to the chosen domain and complete
   every relevant control column; preserve care masks for inactive controls.
3. Keep stage result ownership and oldest-stop priority explicit. An unavailable
   youngest match must not fall back to an older producer. Never replay an
   already accepted future memory transaction.
4. Extend the existing behavioral fixture at the relevant boundary. Keep
   generated MLIR, SystemVerilog, and simulator binaries out of version control.
5. Add complete profile/SoC integration only once its declared features work;
   a decoder domain is not an architectural profile claim.

MEM resolution is currently same-cycle qualification for the token exposed on
`memory_stage`. It is not permission to return an untagged response later.
Future memory/deferred execution needs explicit ownership and precise-fault
handling before younger retirement. The first slice intentionally has no
unused deferred write port, scoreboard, or advertised profile configuration.

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
It compares architectural retirement against an independent sequential model,
not internal register names. External qualification is matched to public MEM
PCs; no test-only RTL switches or hierarchical state mutations are used.

The fixture runner invokes the repository-managed Racket wrapper. Run host
checks through `tools/run-racket-tests.sh` and other elaboration through
`tools/run-racket.sh`; do not bypass the managed compiled root. After changing
fixture ownership, confirm `--group cores-execution-datapath --list-fixtures`
includes `rv2wide-core`. No new SoC or software CI configuration belongs to this
initial execution-slice milestone.
