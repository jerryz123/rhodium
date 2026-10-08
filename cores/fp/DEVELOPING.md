<!-- Routes shared FP source changes, dependency boundaries, and focused verification. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing shared floating point

Read [README.md](README.md) for public controls, service timing, and register
semantics. This guide owns component placement and standalone validation.
The repository [package graph](../../rhodium/DEVELOPING.md) is authoritative.

## Architecture and ownership

This package may import architectural RISC-V descriptors/helpers, HardFloat,
public Rhodium libraries, and Flow. It must not import any named core,
simulator, backend, example, or test. Decode consumes canonical instruction
catalogs; numeric datapaths consume types and operand transactions, never
instruction tables. Keep accepted tags opaque through arithmetic and retagging.

Named cores own authorization, physical write-cycle reservations, destination
scoreboards, LSU ordering, architectural flags, and trap/retirement policy.
The shared register file owns storage/forwarding only. Keep those policies in
the caller rather than adding a dependency back toward a core.

## Implementation map

| File | Responsibility |
|---|---|
| [`types.rhdl`](types.rhdl) | Precision, register-use, and execution controls |
| [`profile.rhm`](profile.rhm) | Implemented XLEN/FP/half/Zfa capability combinations |
| [`decode.rhdl`](decode.rhdl) | Metadata-derived register use, partial execution relations, and standalone decoder |
| [`bundles.rhdl`](bundles.rhdl) | Opaque-tag operand request/result and retagging |
| [`register-file.rhdl`](register-file.rhdl) | Three-read, two-write FPR storage and forwarding |
| [`load-store.rhdl`](load-store.rhdl) | FP load boxing and raw low-precision store shaping without LSU policy |
| [`datapath.rhdl`](datapath.rhdl) | Fixed F/D/half/Zfa execution and exact per-operand promotion |
| [`div-sqrt.rhdl`](div-sqrt.rhdl) | One active variable operation with retained terminal state/tag |
| [`timing.rhdl`](timing.rhdl) | Shared fixed-operation return-delay descriptor |
| [`authorization.rhdl`](authorization.rhdl) | Opaque Valid-owner alignment to fixed returns, without admission or retirement policy |
| [`execute.rhdl`](execute.rhdl) | Lane routing and fixed-return timing, with separate scheduled and buffered return adapters |
| [`tests/`](tests/) | Shared decode contracts and standalone behavioral fixtures |

## Change workflow

1. Change physical types and canonical mappings together, preserving partial
   care masks. Do not introduce a duplicate instruction-kind enum.
2. Keep boxing, canonical NaNs, rounding, and exception contributions aligned
   with the architectural helpers. Preserve individual operand precisions
   through retagging and promotion.
3. Preserve fixed return timing and variable-result retention. Scheduled mode
   may not buffer a promised fixed write; elastic mode must reserve capacity
   before launching nonstallable work. Both paths must drain fairly.
4. Update public behavior in README, capability checks, and caller integration
   when changing a supported specialization or timing contract.
5. Run `make check-boundaries` after moves or import changes and
   `make ci-plan-test` after moving executable tests or fixture inventory.
   Generated HDL/MLIR and simulator output are not checked in.

`timing.rhdl` owns fixed return delays and initiation interval one. Keep service
return collision checks, scalar authorization alignment, and vector reservations
on this shared descriptor; do not repeat a literal latency in callers.
Both cores use `align_fp_authorization` for the delay mechanism, retaining their
own authorization, cancellation, and owner/result checks. Cover latency diversity
with `rv2wide-core-fp-late` and `rv5stage-core-rv64d` when changing this helper.

`FpExecutionService` selects `FpScheduledReturns` or `FpBufferedReturns` while
sharing the numeric lanes and timing pipeline. The scheduled adapter owns
variable-result aging; the buffered adapter owns completion credits and held
arbitration. Neither mode duplicates the arithmetic datapath or adds latency
to the other mode.

## Validation

From the repository root:

```sh
tools/run-racket-tests.sh cores/fp/tests/decode-test.rhm
FIXTURES='fp-register-file fp-service fp-scheduled' \
  bash tools/testing/circt/run.sh --simulate-only
```

The host test checks canonical domains, operand metadata, partial execution
controls, and profile capability selection. The register-file bench checks
read/write forwarding and write priority. The two-client service benches
check fixed throughput, mixed fixed/divide/sqrt reordering, opaque/reused tags,
rounding/flags, held results, scheduled returns, bounded drain, and reset.
These fixtures belong to the `cores-components` CI group.

For migrations or public payload changes, also run the existing RV5Stage
`fp-pipeline-test.rhm`, composed decode tests, and the `rv5stage-fp-pipeline`,
`rv5stage-core-rv32f`, `rv5stage-core-rv64d`, and `rv5stage-vector-fp` behavioral
fixtures. The [RV5Stage owner](../rv5stage/fp/DEVELOPING.md) documents its
scalar architecture; the [CIRCT guide](../../tools/testing/circt/DEVELOPING.md)
owns runner modes and artifacts.
