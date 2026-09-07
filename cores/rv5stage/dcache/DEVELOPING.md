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
| Demand-priority lookup, best-effort prefetch admission, arrays, hit mutation, reservation, replacement, gather, refill installation, and transaction arbitration | [`cache.rhdl`](cache.rhdl) |
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
   S1 owns array reads, tag comparison, and word/state selection; its result
   crosses `ValidPipeAlwaysCapture` before S2 checks permissions and launches
   transactions. Reserve S2 capacity before advancing S1. Retain and reread
   younger S1 requests blocked by older S2 work or snoops; never retain their
   stale array results across mutation, refill, or coherence service. Include
   both stages in drain and maintenance ordering, but let snoops pass a retained
   S1 request once its outstanding read and S2 have drained.
3. Publish refill metadata only after the last word, and invalidate a dirty
   victim before reusing its way.
4. Preserve explicit SRAM ownership and priority among core lookup, line
   gather, refill installation, and snoop service.
5. Keep LR/SC reservation invalidation aligned with local mutation,
   replacement, and invalidating snoops. Do not move architectural alignment or
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
FIXTURES='rv5stage-atomic rv5stage-dcache rv5stage-dcache-rv32 rv5stage-memory-router rv5stage-io-mshr' \
  bash tests/backend/run-circt.sh --simulate-only
```

Use the parent [`DEVELOPING.md`](../DEVELOPING.md#focused-validation) for
complete-core integration. Backend fixture names include `rv5stage-dcache` and
`rv5stage-dcache-rv32`; use the backend test
[`DEVELOPING.md`](../../../tests/backend/DEVELOPING.md) for CIRCT and Verilator
modes. Repository wrappers provide a fresh compiled root.
