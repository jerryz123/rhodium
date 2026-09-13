<!-- Guides standalone vector storage/packing changes and their composed behavioral regression. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing vector storage and packing

Read [README.md](README.md) for port timing, geometry, and caller obligations.
Architectural geometry lives in `riscv/isa/vector.rhm`; these modules own the
named core's physical chunk storage and adapters. Do not add instruction
recognition to `cores/simd-alu.rhdl` or hardware dependencies to the pure model.

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

The direct fixture belongs to the `cores-execution` CIRCT shard. ISA/decode,
CSR/VS integration, unrolling, replay, retirement, shared FP/LSU execution, and
architectural advertisement remain separate future changes.
