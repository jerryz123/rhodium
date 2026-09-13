<!-- Routes vector configuration, decode, storage, and integer unrolling to their validation owners. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the vector path

Read [README.md](README.md) for port timing, geometry, and caller obligations.
Architectural geometry lives in `riscv/isa/vector.rhm`; these modules own the
named core's physical chunk storage and adapters. Do not add instruction
recognition to `cores/simd-alu.rhdl` or hardware dependencies to the pure model.

`riscv/isa/v.rhm` owns initial instruction formats and encodings, and
`riscv/rtl/vector.rhdl` materializes stateless vtype/AVL rules. Core decode owns
the vector control column; `decode/core-ctrl.rhdl` alone adds scalar source and
system controls. `vector/csr.rhdl` owns retained vector state. Its parent CSR
file gates writes on successful WB, enforces VS access, and handles traps.
The candidate configuration input is a combinational preview; a separate
`Pulse` authorizes it only after legality and exception checks. Keep preview
independent of that authorization path.
`bundles.rhdl` owns flat packed-beat/result types, context-bearing macro
requests, and lightweight issue tokens. Keep these independent of the parent
pipeline bundle definitions; scalar bundles must not contain packed vector data.
`unroller.rhdl` owns descriptor retention, synchronous-read credits, issue
position, and ordered authorization progress. `execute.rhdl` is combinational:
it adapts the beat to the shared SIMD unit and packs its result, not a separate
pipeline stage. The parent [`vector.rhdl`](../vector.rhdl) owns their composition,
the register bank, and private EX/MEM/WB storage. An atomic fork couples scalar
bookkeeping admission to private operand capture without splitting handshakes.

Configuration follows ordinary serializing system instructions through
`core.rhdl`. Integer issue tokens use its existing EX/MEM/WB boundaries while
packed data follows the vector module's parallel three-cycle path. The core
passes successful WB PCs back as nonstallable authorizations, never packed
register writes. The vector pipeline asserts result alignment, writes its bank,
and returns a last-beat retirement pulse; only that beat updates CSR/PC/NTL
macro state. Cancellation flushes speculative private validity, and the core
must exempt a vector's own last-beat prediction repair from owner cancellation.
The scalar VX snapshot must wait for older EX/MEM GPR producers, because the
ordinary scalar bypass selector describes next-cycle EX, not a retained ID
value. Keep the unroller active through WB drain, including issue gaps, so
interrupts and younger scalar instructions cannot observe partial execution.
Do not turn the experimental VLEN option into a public ISA/profile claim.

`register-file.rhdl` is 3R1W, stores a flat `Vec(32 * VLEN / 64, Bits(64))`, and uses
Flow `map_valid`/`valid_pipe` to snapshot each read. Forward the bit-merged value
at the read edge, not a live write mux after the response register. Otherwise
a later completion could alter a prior read or create an in-place ALU loop.
One write port uses row-local masked hold feedback, never a destination snapshot
captured by an earlier micro-op or an extra indexed read port for write merging.

`packing.rhdl` handles runtime SEW, broadcasting, lane enables, widening halves,
and destination packing. Global element position is distinct from enabled-lane
count. Keep overflow bits until destination bounds are checked. Local `legal`
outputs are not architectural group/overlap permission or WB authorization.

The composed `tests/vector-fixture.rhdl` captures request controls alongside
the bank read, executes the actual SIMD ALU, and optionally commits its result
through the same masked port used for initialization. Its independent SV
scoreboard checks public read/write behavior at VLEN 128, 256, and 512,
including in-place operations, register boundaries, bit-granular forwarding,
partial bodies, broadcasts, comparison packing, widening, and reset. Do not
replace this with internal register-shape assertions or elaboration-only tests.

Run from the repository root with one fresh compiled root:

```sh
export PLTCOMPILEDROOTS="$(mktemp -d)"
tools/run-racket-tests.sh riscv/tests/vector-test.rhm
FIXTURE=rv5stage-vector bash tests/backend/run-circt.sh --simulate-only
make check-boundaries
```

For configuration, run `riscv/tests/vector-isa-test.rhm`,
`riscv/tests/csr-test.rhm`, `cores/rv5stage/tests/vector-ctrl-test.rhm`,
and the `rv5stage-vector-control`,
`rv5stage-vector-control-rv32`, and `rv5stage-vector-config` backend fixtures.
The first two compose real decode/CSR state with independent RV32/RV64 SV
checks. The last executes configuration, dependent scalar results, CSR reads,
branch squash, integer writes, macro-only `minstret`, VS Dirty, empty-body
retirement, and illegal register groups through the real core/frontend. Its
SV observer binds only to the reusable VRF's public write port, never storage.
This exercises the production vector pipeline's issue/commit alignment and
last-beat feedback through the scalar core, not a replacement execution model.
Two full LMUL=8 streams require sixteen consecutive WB writes each, with exact
row/data/mask checks across private EX/MEM/WB overlap and in-place reuse.
The signature-memory model rejects each store once, then retains readiness
until acceptance, exercising replay without periodic readiness/retry phase lock.
The `rv5stage-vector-unroller` and `rv5stage-vector-unroller-rv32` fixtures
compose real decode/VRF/execute with a flushable WB boundary. An independent
element model checks all decoded integer operations, SEW/LMUL, partial bodies,
mask writes, in-place operations, randomized issue stalls, authorized-prefix
retry, and cancellation. Keep the retry test's downstream flush explicit.
Changes to shared CSR payloads also require `rv5stage-csr` and the RV32/RV64
`rv5stage-zihpm-*` fixtures. These fixtures belong to `cores-execution`.
