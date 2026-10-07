<!-- Defines shared predictor ownership, implementation paths, and focused validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing branch prediction

Read [README.md](README.md) for the component APIs and deliberate lookup-width
limit. This guide owns predictor state, shared payloads, RISC-V hint helpers,
and direct behavioral fixtures. Follow [core ownership](../DEVELOPING.md) and
the [repository package graph](../../rhodium/DEVELOPING.md).

## Architecture and ownership

`protocol.rhdl` depends only on architectural XLEN and the public authoring
surface. BTB and RAS implementations consume that protocol; protocols must not
import implementations. No shared predictor imports a named core, fetch
consumer, backend, or simulator.

Fetch and execution pipelines own update qualification, prediction propagation,
packet truncation, recovery priority, and architectural context changes. RAS
resolved state is not intrinsically retirement state: callers choose the
resolution boundary. Keep those choices outside this package.

## Implementation map

| File | Responsibility |
|---|---|
| `protocol.rhdl` | Prediction, branch training, and RAS event payloads |
| `btb.rhdl` | Associative low-address entries, shared upper tags, local counters, discovery, and replacement |
| `ras.rhdl` | Canonical/compressed RISC-V hints and speculative/resolved bounded stacks |
| `tests/ras-fixture.rhdl` | Direct stack and classification test boundary |
| `tests/circt/` | Shared BTB/RAS emitters and cycle-visible Verilator oracles |

## Change workflow

Update the public contract when changing lookup geometry, event priority,
replacement, or stack recovery. Preserve exact full-address matching when
changing shared tag storage; replacement must invalidate both source and target
references. Keep speculative actions single-shot under caller backpressure.

Lookup supports four- and eight-byte windows. Compare the full block address,
exclude offsets before the cursor, then choose the first matching halfword
position independently of entry allocation order. Four bytes remains the
default, preserving RV5Stage timing and selection. RV2Wide selects eight bytes.
Do not add compatibility forwarding modules under the old named-core paths.

## Validation

Run the direct behavioral fixtures through the managed Racket/CIRCT runner:

```sh
FIXTURES='bpred-btb bpred-btb-wide bpred-ras' bash tools/testing/circt/run.sh --simulate-only
```

They cover counter saturation, address ordering and halfword cursors, entry and
shared-page replacement, training/discovery/invalidation priority, call/return
classification, stack overflow/underflow, and speculative reconciliation.

For changes to shared payloads or extraction boundaries, also run the RV5Stage
`rv5stage-fetch-prediction`, `rv5stage-return-prediction`, and
`rv5stage-branch-prediction` fixtures. Include `event-frontend` when changing
fetch Flow contracts. After moves or import changes run `make check-boundaries`;
fixture names and shared-component CI ownership live in the
[CIRCT manifest](../../tools/testing/circt/run.sh).
