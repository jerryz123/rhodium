<!-- Routes RV5Stage scalar FP ownership and shared-component integration checks. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RV5Stage floating point

Read [README.md](README.md) for scalar ports, hazards, and retirement semantics.
This guide owns the architectural wrapper, not the reusable numeric components;
their contributor guide is [`cores/fp/DEVELOPING.md`](../../fp/DEVELOPING.md).

## Architecture and ownership

This package imports shared FP types, operand transactions, execution, and
register storage from `cores/fp/`. It may also use RISC-V architectural helpers,
public Rhodium libraries, Flow, and passive observation declarations. It must
not import caches, MMU implementation, `core.rhdl`, backends, simulators, or
tests. No shared FP component imports this wrapper. See the repository
[package graph](../../../rhodium/DEVELOPING.md).

`core.rhdl` owns WB authorization, issue arbitration, physical write calendars,
memory requests, and integer-result retirement. The scalar wrapper owns FPR
reservations/hazards, load boxing, operand snapshots, completion adaptation,
and architectural-state update events. Canonical FP instruction mappings live
in [`cores/fp/decode.rhdl`](../../fp/decode.rhdl); the named
[`core-ctrl.rhdl`](../decode/core-ctrl.rhdl) joins them with scalar columns.

## Implementation map

| File | Responsibility |
|---|---|
| [`bundles.rhdl`](bundles.rhdl) | Scalar issue/completion, scalar execution owner, and LSU payloads |
| [`pipeline.rhdl`](pipeline.rhdl) | FPR scoreboard, load/store bridges, vector FPR reservation/write, and completion/state updates |
| [`../../fp/`](../../fp/DEVELOPING.md) | Shared types/decode, tagged arithmetic service, and 3R2W register file |

`RV5StageFpScalar` exposes operand execution ports. `RV5StageFpPipeline`
composes it with an elastic service for standalone use; the core instead
shares a scheduled service with vector clients. Scalar context/destination/rd
belong only to the wrapper's opaque execution tag.

## Change workflow

1. Keep compute and deferred-load admission at authorized WB. Rejected work
   must replay without retirement, scoreboard reservations, or arithmetic effects.
2. Preserve the enabled/disabled interface shape. Read port 0 serves scalar
   issue or the read-only WB vector-scalar snapshot while issue is idle.
3. Keep vector FPR reservation/write separate from arithmetic. Reserve before
   macro admission, exclude scalar FP work until completion, and clear only
   on the authorized write. Never feed that write back into its own admission.
4. Preserve load-hit/deferred-load write exclusion, NaN boxing, and FS/flag
   updates. Speculative store probes are not accepted work and cannot hold
   architectural trap/interrupt drain.
5. Preserve passive post-boxing load-write taps through `cosim-source.rhm`.
   The selected parent observer owns capture; functional RTL imports no simulator.

Keep shared arithmetic changes under its owner and update the public README
when wrapper semantics change. Generated output remains untracked.

## Focused validation

```sh
tools/run-racket-tests.sh cores/fp/tests/decode-test.rhm \
  cores/rv5stage/tests/fp-pipeline-test.rhm cores/rv5stage/tests/core-ctrl-test.rhm
FIXTURES='rv5stage-fp-pipeline rv5stage-core-rv32f rv5stage-core-rv64d' \
  bash tools/testing/circt/run.sh --simulate-only
```

These preserve F/D/Zfh/Zfa arithmetic integration, FPR hazards, LSU bridges,
WB authorization, and flag updates. Add `rv5stage-vector-fp` for shared service
arbitration, retagging, vector reservation, or scalar/vector movement changes.
Run the [shared FP fixtures](../../fp/DEVELOPING.md#validation) for arithmetic,
service, or register-file changes. Moves/import changes require
`make check-boundaries`; fixture moves also require `make ci-plan-test`.
The [CIRCT guide](../../../tools/testing/circt/DEVELOPING.md) owns runner modes.
