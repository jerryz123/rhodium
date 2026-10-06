<!-- Defines shared-cache implementation ownership and focused validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing shared caches

Read [README.md](README.md) for the public protocol and timing contract, then
[core ownership](../DEVELOPING.md). This guide owns the shared physical cache,
not named-core LSU, translation, or retirement policy.

## Architecture and ownership

Named cores depend on this package; it must never import them. The package uses
public Rhodium/Flow, CHI, physical atomic/locality helpers, and the shared hart
CHI map. Follow the [package graph](../../rhodium/DEVELOPING.md).
`Context :: DataType` is opaque: do not introduce register numbers, FP precision,
vector slots, trap causes, or core/PTW routing decisions into cache state.

## Implementation map

| File | Responsibility |
|---|---|
| `config.rhm`, `geometry.rhdl` | Immutable VIPT geometry and way/lane masks |
| `operation.rhdl`, `protocol.rhdl` | Physical operations, fixed-cycle lookup, authorization, completion |
| `l1d/cache.rhdl` | SRAM scheduling, set-isolated hit-under-miss, retained service, coherence, LR/SC |
| `l1d/arrays.rhdl`, `l1d/store-buffer.rhdl` | Synchronous storage and committed byte hazards |
| `chi/flits.rhdl` | Transaction response profiles and flit construction |
| `chi/refill.rhdl`, `chi/writeback.rhdl`, `chi/snoop.rhdl` | Retained acquisition, victim, and snoop lifetimes |
| `io-mshr.rhdl` | Opaque-context committed IO retention, from admission through final response |
| `chi/uncached.rhdl`, `chi/write-unique.rhdl` | Nonallocating RN-I service and retryable partial-width write transport |

## Change workflow

Keep S0 early lookup, S1 physical resolution, and S2 store authorization aligned
with the integrating core. S3/S4 are elastic authorized-service stages, not fixed
core cycles. Preserve registered response capacity and owner retention across
backpressure. Retained S3 lookups must reread after older mutation or snoops;
do not reuse stale synchronous array results.

Store hazards include the current enqueue. A miss protects its whole set;
independent hits do not acquire the miss engine. Publish a refilled line only
after all words are installed. Coherence and committed response ownership are
never canceled by pipeline flush. Preserve the bounded LR probe-protection
window independently of reservation validity and drain accepted probes.

Keep existing `dcache/*` event names and retained transaction lineage stable;
named-core integration must not summarize an entire cache as a fake trace
stage. Public timing and context transport remain
observable integration contracts.

## Validation

Run affected behavioral owners through the managed fixture runner:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/dcache-test.rhm
FIXTURES='rv5stage-dcache rv5stage-dcache-rv32 rv5stage-load-hit rv2wide-cache' \
  bash tools/testing/circt/run.sh --simulate-only
make check-boundaries
```

The RV5Stage cache fixtures specialize L1D with architectural context and
exercise tags, byte masks, LR/SC, atomics, snoops, backpressure, and refill.
The RV2Wide fixture exercises the real shared L1D from two-wide instruction
issue through retirement, delayed completion, replay, and physical faults.
For CHI engine changes also run `rv5stage-compack` and `cache-copyback`;
for store-buffer changes run `cache-store-buffer`.
The standalone copyback and store-buffer fixtures live in this package's
`tests/`; RV5Stage's acknowledgement fixture also exercises its instruction
line reader and therefore stays with that core.

For nonallocating service changes, run `rv5stage-uncached`, `rv5stage-io-mshr`,
and `rv5stage-memory-router`. These integrated fixtures cover fetch cancellation,
data priority, committed IO retention, byte placement, block zero, and ordered
routing. Keep cancellation at the pre-CHI-request boundary; never clear an
accepted transaction's response owner on a requester flush.
