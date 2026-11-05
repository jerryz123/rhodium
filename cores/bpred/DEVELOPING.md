<!-- Defines shared predictor ownership, implementation paths, and focused validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing branch prediction

Read [README.md](README.md) for the component APIs and deliberate lookup-width
limit. This guide owns predictor state, shared payloads, RISC-V hint helpers,
and direct behavioral fixtures. Follow [core ownership](../DEVELOPING.md) and
the [repository package graph](../../rhodium/DEVELOPING.md).

## Architecture and ownership

`protocol.rhdl` depends on architectural XLEN, shared `cores/pc.rhdl`, and the public authoring
surface. BTB and RAS implementations consume that protocol; protocols must not
import implementations. No shared predictor imports a named core, fetch
consumer, backend, or simulator.

Fetch and execution pipelines own update qualification, prediction propagation,
packet truncation, recovery priority, and architectural context changes. RAS
resolved state is not intrinsically retirement state: callers choose the
resolution boundary. Keep those choices outside this package.
Target-width policy belongs to those callers too. `prediction_target` normalizes
speculative destinations; BTB writes use it before shared-page allocation, while
both RAS copies physically store `CompactPc` at the selected width. BTB `pc_width` independently
bounds source admission without aliasing: qualify lookup, write commit, and
invalidation with exact sign-extendability. Shared tags span the larger source
and target width, so target-only compaction cannot discard source bits.
Use `CompactPc.fits` for source admission and `.expanded()` after joining BTB
page tags and low bits; retain the shared-page representation itself.
Out-of-range writes must not advance either replacement cursor or evict pages;
they retain update-over-discovery priority. Preserve invalidation-over-write
priority even on an out-of-range invalidation. Never narrow architectural
branch-update payloads. Default XLEN keeps standalone predictors lossless.
Named frontends select widths covering their successful Sv39/Bare PCs and
normalize every fallback before steering and recording it.

Keep active BTB/RAS implementations separate from `DisabledBtb` and `DisabledRas`.
The caller selects the circuit from its entry-count configuration; active
implementations accept only positive depths. Disabled circuits retain the port
contract without allocating predictor state.

## Implementation map

| File | Responsibility |
|---|---|
| `protocol.rhdl` | Target/direction predictions, saved-index training, and RAS event payloads |
| `statistics.rhdl` | Pure failure classification and passive count declarations; callers qualify the winning recovery |
| `btb.rhdl` | Associative low-address entries, shared upper tags, local counters, discovery, and replacement |
| `counter-table.rhdl` | Single-write-port banked counter storage and reset-only row sweep shared by the direction predictors |
| `bht.rhdl` | Standalone BHT with its own reset sweep and saved-index training |
| `tournament.rhdl` | Independent bimodal/gshare lookup ports and saved-disagreement chooser training |
| `ras.rhdl` | Canonical/compressed RISC-V hints and speculative/resolved bounded stacks |
| `tests/ras-fixture.rhdl` | Direct stack and classification test boundary |
| `tests/rsim/` | Shared BTB/BHT/RAS emitters and cycle-visible C++ oracles |

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
Keep prediction classification separate from functional recovery. The statistics
helper partitions failure causes; core arbitration supplies the actual recovery
event. RV5Stage retains the EX classification in its existing EX/MEM token, while
RV2Wide classifies its selected MEM owner. No counter may influence issue or recovery.
The public names and baseline policy live in [simulation statistics](../../sims/README.md#rtl-statistics).

## Validation

Run the direct behavioral fixtures through the managed Racket/rsim runner:

`prediction-statistics` exhausts classification/priority, event gating, and reset
with the production collector. `rv2wide-core` also checks sampled retirement and
issue counters against public ports throughout its existing behavioral streams.

```sh
FIXTURES='bpred-btb bpred-btb-compact bpred-btb-wide bpred-btb-external bpred-bht bpred-tournament bpred-ras bpred-ras-compact' python3 tools/testing/rsim/run.py
```

They cover counter saturation, address ordering and halfword cursors, entry and
shared-page replacement, training/discovery/invalidation priority, call/return
classification, stack overflow/underflow, and speculative reconciliation.
The compact variants use 45-bit destinations and the same independent oracle,
including positive/negative aliases, source-tag isolation, discovery, and RAS
restoration. Named-core prediction fixtures check that the followed destination
matches the retained hint and that full-width recovery still wins; the RV5Stage
branch fixture checks exact fault PC/value after an aliased poison-store stream.
The `bpred-btb-bounded` and `bpred-btb-source-bounded` fixtures reuse the entry/page
replacement oracle with bounded sources and either bounded or full-width targets.
They check both sign boundaries, the final halfword before XLEN wrap, every
discarded source bit on lookup/update/discovery/invalidation, ignored-write
priority, and unchanged replacement after rejected writes. The `wide-bounded`
and `external-bounded` variants retain eight-byte earliest-branch ordering and
caller-supplied direction behavior. Run `tests/target-width-test.rhm` through the
repository test wrapper for invalid source/target width combinations.
The tournament oracle independently models all three tables, including chooser
saturation, agreement, delayed predictions, aliased consecutive updates, and
training suppression through the final scrub edge. Keep **one** sweep per
tournament, not one per table. Use the existing memory write port for both
scrubbing and training; table contents have no reset or valid bits. Pair the
table fixtures with `rv2wide-bht` when changing initialization or context
recovery. External-direction BTB tests cover every bank, earliest-target
selection, unconditional entries, and disagreement with local counters.

For changes to shared payloads or extraction boundaries, also run the RV5Stage
`rv5stage-fetch-prediction`, `rv5stage-return-prediction`, and
`rv5stage-branch-prediction` fixtures. Include `event-frontend` when changing
fetch Flow contracts. After moves or import changes run `moon run rhodium:check-boundaries`;
fixture names and shared-component CI ownership live in the
[rsim inventory](../../tools/testing/rsim/fixtures.tsv).
