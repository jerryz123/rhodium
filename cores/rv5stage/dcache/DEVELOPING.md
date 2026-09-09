<!-- Guides contributors through implementing and validating RV5Stage's data cache. -->

# Developing the RV5Stage data cache

Read the L1D [README](README.md) for its public request/response, hit, refill,
writeback, snoop, atomic, LR/SC, replacement, and deliberate-limit contracts.
This guide owns implementation placement and contributor validation.

## Architecture and ownership

The L1D package owns the data-access protocol, synchronous arrays, hit path,
coherence state, mutation, blocking allocation/acquisition, replacement, and
LR/SC reservation, and the non-allocating data IO-MSHR. The parent core owns
virtual translation, alignment faults, PMA routing, architectural fence ordering,
and the external CHI boundary.

Keep L1D independent of L1I. Reuse parent cache parameters and the sibling CHI
package's refill, write-unique, writeback, and data-snoop engines rather than
importing the instruction-cache package.
[`../../check-boundaries.sh`](../../check-boundaries.sh) enforces that split.

## Implementation map

| Concern | Owner |
|---|---|
| Core-facing request and response bundles | [`protocol.rhdl`](protocol.rhdl) |
| One-entry uncached data admission, retained request, and completion lifetime | [`io-mshr.rhdl`](io-mshr.rhdl) |
| Synchronous tag/state and byte-masked data storage | [`arrays.rhdl`](arrays.rhdl) |
| Two committed entries, physical-byte/probe comparisons, FIFO order, and bounded age | [`pending-stores.rhdl`](pending-stores.rhdl) |
| Shared pipeline decisions, SRAM scheduling, prefetch admission, reservation, replacement, gather, refill installation, and transaction arbitration | [`cache.rhdl`](cache.rhdl) |
| Shared cache geometry | [`../cache.rhdl`](../cache.rhdl) |
| Retry-aware complete-line refill | [`../chi/refill.rhdl`](../chi/refill.rhdl) |
| Ownership acquisition and partial writes | [`../chi/write-unique.rhdl`](../chi/write-unique.rhdl) |
| Dirty-victim drain | [`../chi/writeback.rhdl`](../chi/writeback.rhdl) |
| Clean and dirty snoop transaction lifetime | [`../chi/snoop.rhdl`](../chi/snoop.rhdl) |
| Core/MMU/CHI integration | [`../rv5stage.rhdl`](../rv5stage.rhdl) |
| Host configuration and protocol metadata | [`../tests/dcache-test.rhm`](../tests/dcache-test.rhm), [`../tests/transaction-engines-test.rhm`](../tests/transaction-engines-test.rhm) |
| CIRCT/Verilator fixtures | [`../../../tests/backend/`](../../../tests/backend/DEVELOPING.md#fixture-and-artifact-ownership) |

## Change the cache

1. Preserve ordered Decoupled requests and non-backpressurable Valid responses,
   including completion metadata for stores, atomics, and deferred writeback.
   Keep readiness structural even when an empty request buffer is bypassed by
   a paired virtual read and physical resolution. Queued physical requests win
   the lookup port; unresolved virtual reads cannot produce a lookup token.
   Geometry rejection belongs to `../profile.rhm`.
2. Keep the one-request-per-cycle load-hit path separate from blocking miss,
   acquisition, gather, writeback, and installation state.
   The speculative `pipeline_lookup` read uses only EX's virtual page offset.
   `pipeline.request` supplies the MEM physical tag and operation controls; return its
   matching SRAM value directly to the core MEM/WB register. Do not insert the
   authorized transaction path's S2/response registers into that hit path.
   Registered read ownership and page-offset matching prevent consuming another
   request's SRAM response. Return an explicit replay for blocked reads, not a
   slow-service request. Store lookup retains only a one-cycle candidate; WB
   authorization alone enqueues it. Compare all committed bytes, including the
   current enqueue, without using global drain as a hit-readiness condition.
   Only enqueue may clear a matching reservation for a buffered ordinary store.
   Keep SRAM arbitration independent of logical word/byte conflicts, and keep
   same-line snoop draining independent of unrelated probes. Do not allow a
   committed entry's way to be replaced or downgraded before its drain.
   For authorized transactions,
   S1 owns array reads, tag comparison, and word/state selection; its result
   crosses `ValidPipeAlwaysCapture` before S2 checks permissions and launches
   transactions. Reserve S2 capacity before advancing S1. Retain and reread
   younger S1 requests blocked by older S2 work or snoops; never retain their
   stale array results across mutation, refill, or coherence service. Include
   both stages in drain and maintenance ordering, but let snoops pass a retained
   S1 request once its outstanding read and S2 have drained.
3. Publish refill metadata only after the last word, and invalidate a dirty
   victim before reusing its way. Capture the cache's install disposition in
   transaction context separately from architectural locality and new-way
   allocation. A non-allocating load must complete from the acknowledged clean
   transaction buffer without SRAM writes, victim handling, replacement-pointer
   movement, or reservation invalidation.
4. Preserve explicit SRAM ownership and priority among core lookup, line
   gather, refill installation, and snoop service.
5. Acquire Unique for LR without treating it as a store for translation,
   faults, dirty state, or refill mutation. SC authorization is a local owned
   hit decision; never retain it in a refill context. Keep probe-protection expiry
   separate from reservation validity. Preserve mutation/replacement invalidation
   and probe state changes under the [LR/SC contract](README.md#lrsc-reservation).
   Gate new snoop admission rather than masking an accepted snoop's pending
   state. Protect the CompAck-to-install interval and give waiting lookups a
   bounded turn after `snoop.completed`. Countdown progress must not depend on
   core, memory, or snoop readiness. Do not move architectural alignment or
   PMA faults into the cache.
6. Keep prefetch response-free and demand-priority; write intent may acquire
   UniqueClean ownership but must not mutate data or make a line dirty.
7. Keep IO-MSHR admission independent of RN-I instruction ownership. Reject
   unsupported operations before allocation, retain the request until issue,
   and release the slot only on final completion. Do not connect fetch flush
   to committed data state or use engine-wide `drained` for data-slot occupancy.
8. Test hit, miss, refill, atomic, and coherence behavior in compiled fixtures;
   keep host coverage to configuration and protocol metadata. Update
   [README.md](README.md) when public timing, state, traffic, or limits change.

## Focused validation

Use the host check for geometry, public types, and protocol metadata:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/dcache-test.rhm
```

Test cache, transaction, and atomic behavior through compiled fixtures:

```sh
FIXTURES='rv5stage-pending-stores rv5stage-load-hit rv5stage-atomic rv5stage-dcache rv5stage-dcache-rv32 rv5stage-lrsc-progress rv5stage-memory-router rv5stage-io-mshr' \
  bash tests/backend/run-circt.sh --simulate-only
```

Use the parent [`DEVELOPING.md`](../DEVELOPING.md#focused-validation) for
complete-core integration. Backend fixture names include `rv5stage-dcache` and
`rv5stage-dcache-rv32`; use the backend test
[`DEVELOPING.md`](../../../tests/backend/DEVELOPING.md) for CIRCT and Verilator
modes. Repository wrappers provide a fresh compiled root.

The RV64 cache bench covers cold and shared-hit LR ownership, a probe offered
at CompAck, delayed SC under pending eviction traffic, protection expiry, repeated LR,
and reacquisition after revocation. RV32 and RV64 also require SC success after
a quiet delay beyond the protection window, without new CHI traffic.
`rv5stage-lrsc-progress` connects two actual
L1Ds to a two-set, one-way inclusive Home and CHI SRAM through registered,
round-robin channel transport. It forces read-only
LLC replacement during delayed SC, checks dirty-data preservation through
eviction, permits an intervening writer after a stalled LR's protection expires, and
completes two competing LR/SC pairs through exclusive acquisition.
These tests do not replace constrained instruction-loop testing through a
complete SoC, including fetch, translation, and network arbitration.
