<!-- Defines shared-cache implementation ownership and focused validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing shared caches

Read [README.md](README.md) for the public protocol and timing contract, then
[core ownership](../DEVELOPING.md). This guide owns the shared physical cache,
not named-core frontend, LSU, translation, or retirement policy.

## Architecture and ownership

Named cores depend on this package; it must never import them. The package uses
public Rhodium/Flow, CHI, physical atomic/locality helpers, and the shared hart
CHI map. Follow the [package graph](../../../rhodium/DEVELOPING.md).
`Context :: DataType` is opaque: do not introduce register numbers, FP precision,
vector slots, trap causes, or core/PTW routing decisions into cache state.

## Implementation map

| File | Responsibility |
|---|---|
| `config.rhm`, `geometry.rhdl` | Immutable VIPT geometry and way/lane masks |
| `udb.rhm` | Reservation and cache-maintenance policy declarations; integrating cores retain split-access and alignment-fault guarantees |
| `access.rhdl` | Speculative lookup classification, whole-access PMA footprint, split-fragment and PTE permissions, and committed route selection |
| `prefetch.rhdl` | Aligned whole-line PMA/PBMT classification and operation-specific permissions, without staging or final eligibility |
| `operation.rhdl`, `protocol.rhdl` | Physical operations, fixed-cycle lookup, authorization, completion |
| `l1d/cache.rhdl` | SRAM scheduling, set-isolated hit-under-miss, retained service, coherence, LR/SC |
| `l1i/cache.rhdl`, `l1i/protocol.rhdl` | Width-selected instruction-block lookup, snapshot residency, replay, invalidation |
| `chi/line-read.rhdl` | Ownership-free coherent RAM and immutable ROM snapshots with opaque context |
| `l1d/arrays.rhdl`, `l1d/store-buffer.rhdl` | Synchronous storage and committed byte hazards |
| `chi/flits.rhdl` | Transaction response profiles and flit construction |
| `chi/attachment.rhdl` | Shared-cache RN-I/RN-F capabilities and hart-to-Home attachment descriptions |
| `chi/refill.rhdl`, `chi/writeback.rhdl`, `chi/snoop.rhdl` | Retained acquisition, victim, and snoop lifetimes |
| `io-mshr.rhdl` | Opaque-context committed IO retention, from admission through final response |
| `uncached-arbiter.rhdl` | Data-first fetch/data arbitration, retained response ownership, and detached-fetch draining |
| `uncached-instruction.rhdl` | Width-selected physical fetch shaping and shared CHI transport composition |
| `ordered-service.rhdl` | Attaches cache and IOMSHR to ordered service, including local-management completion and response merge |
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

Run `rv5stage-speculative-access` and `rv2wide-mmu` together after changing
speculative physical permission checks. They drive the same offers through EX
and check the MEM outcome and physical lookup on the following cycle.

Use `classify_committed_cache_access` for physical admission, service selection,
and uncached ordering. Both ports check atomic alignment. Superscalar also
checks ordinary request alignment at its native-word port. RV5Stage routes
ordinary misaligned loads and stores through split service upstream.
Keep the port's fault handshake and payload adaptation in its caller.
Pass the aligned translated address, fragment byte count, and selected PBMT to
`cache_fragment_permitted`. The physical address can be wider than XLEN.
The MMUs retain fault priority and the address reported for each fragment.

`prefetch.rhdl` shares physical classification, not hint admission. RV5Stage
checks cacheability/permission and cancels staged hints at the clock edge.
RV2Wide's translated adapter additionally requires ordinary idempotent memory,
checks probe availability/address bounds, and gates the outgoing flush cycle.
Keep these differences in the callers; use `rv5stage-mmu-replay` and
`rv2wide-mmu` for prefetch changes.

`OrderedMemoryService` takes already-classified streams and opaque payloads.
Preserve registered IO capacity as the cached-admission condition, separately
from drain, to avoid a request/response-ready cycle. `attach_ordered_memory`
wires that service to the cache and IOMSHR. Each core supplies its IO width and
context, qualifies requests, handles fault sidebands, and adapts byte lanes.
Do not introduce new buffering on the cached or IO request paths. The sole
local-management slot replaces the callers' previous one-entry queues.

`UncachedInstructionMemory` composes existing ownership and transport without
another request stage. The frontend controls cancellation; committed data never
inherits it. Keep fetch response draining on the detached owner and retain data
priority even while a canceled instruction reply drains. When driven by an
IOMSHR, specialize the transport context to `Bool`: only the slot owns the
caller's return tag, and only a data response handshake releases that slot.
Arbitrate commands, not store data. The slot positions data and byte enables
within a doubleword at admission. Wire that payload directly to the transport;
its accepted address selects the CHI lane. Never source DAT from a live
request or recapture it on DBID.

Keep existing `dcache/*` event names and retained transaction lineage stable;
named-core integration must not summarize an entire cache as a fake trace
stage. Public timing and context transport remain
observable integration contracts.

## Validation

Run affected behavioral owners through the managed fixture runner:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/dcache-test.rhm
python3 tools/testing/rsim/run.py --fixture rv2wide-cache
FIXTURES='rv5stage-dcache rv5stage-dcache-rv32' python3 tools/testing/rsim/run.py
FIXTURES='rv5stage-load-hit' python3 tools/testing/rsim/run.py
moon run rhodium:check-boundaries
```

The RV5Stage cache fixtures specialize L1D with architectural context and
exercise tags, byte masks, LR/SC, atomics, snoops, backpressure, and refill.
The RV2Wide fixture exercises the real shared L1D from two-wide instruction
issue through retirement, delayed completion, replay, and physical faults.
For CHI engine changes also run `cache-compack` and `cache-copyback`;
for store-buffer changes run `cache-store-buffer`.
For row-width changes run `cache-wide-l1d` (coherent service, speculation,
masked stores, and dirty snoops) and `load-store-wide` (scalar lane placement).
Keep the native data-array instance used by the Mini SRAM map intact; the
16-byte specialization instantiates a separate byte-masked data SRAM per way.
The standalone copyback, store-buffer, instruction-cache, and acknowledgement
fixtures live in this package's `tests/`. Follow the [L1I guide](l1i/DEVELOPING.md)
for 32/64-bit block tests and named-core fetch integration coverage.

For nonallocating service changes, run `rv5stage-uncached`, `rv5stage-io-mshr`,
`rv5stage-io-mshr-rv32`, and `rv5stage-memory-router`. For fetch/data arbiter changes include RV2Wide's
`rv2wide-fetch` fixture. These integrated fixtures cover fetch cancellation,
data priority, committed IO retention, byte placement, block zero, and ordered
routing. Keep cancellation at the pre-CHI-request boundary; never clear an
accepted transaction's response owner on a requester flush.
