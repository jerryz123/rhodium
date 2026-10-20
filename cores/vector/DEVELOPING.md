<!-- Defines shared vector ownership, extraction boundaries, and focused behavioral validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing shared vector execution

Read [README.md](README.md) for the public service contract and
[core ownership rules](../DEVELOPING.md) before changing this family.

## Architecture and ownership

This package may consume architectural `riscv/` types, shared processor
components, HardFloat contracts, and Rhodium/Flow. It must not import a named
core, its configuration, pipeline payload, or retirement policy. The
[package graph](../../rhodium/DEVELOPING.md) is authoritative.

`VectorExecution` accepts already nonspeculative descriptors. Its opaque
`Context` travels with memory tokens; scalar retirement records do not belong
in shared bundles. Integer service tags use `../integer-execution.rhdl`, and
GPR writes use `../register-write.rhdl`. They are independent of register-file
port counts and scalar scheduling.

Keep instruction IDs, completion slots, and the sequencer cursor distinct.
Return routing uses retained slot ownership, never the live descriptor. A retry
restores the unauthorized checkpoint without deleting accepted memory ownership.
Keep simultaneous fixed result lanes independent; Flow composition must not
introduce buffering or lossy arbitration on a nonstallable return.

## Implementation map

| Files | Responsibility |
|---|---|
| `decode.rhdl` | Component control relations, operand modes, configuration request formation, and operation legality |
| `bundles.rhdl`, `packed-bundles.rhdl` | Descriptor, beat, response, and packed transport contracts |
| `pipeline.rhdl` | Execution composition, shared-service scheduling, and register-port arbitration |
| `sequencer.rhdl`, `operand-fetch.rhdl` | Single descriptor cursor/checkpoint and fixed-latency operand capture |
| `instructions.rhdl`, `dependencies.rhdl` | Persistent instruction owners and register-row hazards |
| `completion.rhdl`, `slots.rhdl` | Result writeback, accepted ownership, and ordered metadata reclamation |
| `register-file.rhdl`, `packing.rhdl`, `mask.rhdl`, `execute.rhdl` | VRF/mask shadow and packed SIMD operand/result shaping |
| `packed-prepare.rhdl`, `packed-load.rhdl`, `splat-load.rhdl`, `load-response.rhdl` | Packed and splat transport preparation, response assembly, and retained carry |
| `geometry.rhdl`, `precheck.rhdl` | Beat geometry and conservative certification footprints |
| `admission.rhdl` | Descriptor credit, head certification, pinned-window release, queued-kind summaries, and registered outcomes |
| `replay.rhdl` | Durable request retention/retry cursors and independent store acknowledgements |
| `instruction-metadata.rhm` | Canonical base decode domains and scalar operand/destination metadata |
| `fp.rhdl`, `muldiv.rhdl` | Shared arithmetic operands and tagged result conversion |

`../mmu/vector.rhdl` owns certification/probe contracts and
`../mmu/vector-window.rhdl` owns the pinned two-page window. The named MMU owns
lookups, walks, permissions context, and invalidation. Named vector wrappers
compose `VectorAdmission` with execution and their LSU adapter. They retain WB
policy, fault-provenance types, drain qualification, and descriptor queue timing.
The memory adapters retain fixed lookup stages, fault interpretation, split
ownership, and cache response shaping; `VectorReplay` owns only durable request
storage and its ordered attempt/release cursors.

## Change workflow

Keep this boundary independent of RV5Stage when adding another caller. Reuse
component decode relations, but compose scalar decode columns in that caller.
Do not add compatibility re-exports through the old named-core paths.
Preserve trace event labels and passive observation fields when moving code;
instrumentation consumers depend on ownership and timing, not source location.

`VectorReplay.skip` resumes attempts after an oldest owner accepted by a split
transaction; `retry` instead rewinds to that owner. Neither event deletes
accepted response ownership. Recovery suppresses attempts while still allowing
durable capture. Keep acknowledgement readiness independent of cache commit.
Preserve caller-selected pipelined versus registered descriptor credit: changing
it can introduce a combinational cycle through WB and LSU readiness.

Move direct behavioral fixtures with their implementation. Shared fixtures
decode vector controls directly rather than instantiating a named core decoder.
Integrated CSR/decode, admission, MMU/cache, and software tests remain with their
named core. Existing `rv5stage-vector-*` fixture IDs are retained as runner
selectors; the corresponding direct emitters and rsim drivers now live here.
The generic host clock/response scoreboard helper lives in
`../tests/rsim/driver.hpp`; named-core memory encodings remain with their caller.

## Validation

For extraction or service-boundary changes, run the existing focused fixtures:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/vector-ctrl-test.rhm cores/rv5stage/tests/core-ctrl-test.rhm
FIXTURES='rv5stage-vector-overlap rv5stage-vector-packed rv5stage-vector-sequencer-rv32 rv5stage-vector-admission' python3 tools/testing/rsim/run.py
make check-boundaries
```

The overlap fixture covers persistent response ownership across sequencer
replacement; packed and RV32 fixtures cover transport and row geometry. The
RV5Stage admission fixture checks the caller boundary. Add its vector-memory
and MMU replay fixtures for translation/LSU behavior changes, FP/muldiv fixtures
for shared arithmetic changes, and cosim/event fixtures for observation changes.
Use the managed Racket wrappers and keep generated HDL/native builds untracked.

For admission/replay extractions, cover both callers with
`rv2wide-core-vector`, `rv2wide-core-vector-rv32`, `rv2wide-vector-memory`,
`rv5stage-vector-admission`, `rv5stage-vector-memory`, and
`rv5stage-vector-memory-one-slot`. These preserve registered WB credit,
precise versus certified outcomes, split ownership, and depth-one ring indexing.
