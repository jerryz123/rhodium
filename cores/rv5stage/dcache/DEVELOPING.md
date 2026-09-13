<!-- Guides implementation and validation of RV5Stage's set-isolated hit-under-miss data cache. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

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

### Internal naming and organization

In `cache.rhdl`, `s0_` names the core-aligned EX admission/read launch,
`s1_` names MEM's synchronous SRAM result and physical-tag/hit decision, and
`s2_` names WB's retained store candidate, authorization, and slow-request
admission. Name a pipeline register for the stage consuming its current value,
not the stage supplying its next value. The fast hit path stays contiguous
near the top of the circuit.

Authorized requests extend the cache pipeline beyond WB: `s3_` names the
retained SRAM lookup, and `s4_` names captured resolution, permission checks,
and transaction launch. Queueing or rereads can stall this path, so S3 and S4
are cache processing stages, not fixed cycles after the core's WB. Retries
reenter the `s2_` read-admission logic without repeating core authorization.
Keep transaction-spanning state under its engine name, such as `miss_*`,
`refill_*`, `snoop_*`, or `reservation_*`.

Use flow chains for channel arbitration, Valid payload transformation, and
engine-generated offers. Preserve fixed-priority input order and unconditional
payload capture at `ValidPipeAlwaysCapture` boundaries. Shared scheduler grants
are combinational wires, not additional pipeline stages. Keep SRAM owner
priorities, store authorization, and engine state transitions explicit; do not
replace non-backpressurable completion selection with a dropping Valid arbiter.

S4 hit/refill/eviction branches carry the complete resolved bundle; build
transaction commands and capture mutation/gather state from the consuming
branch's payload. Terminate accepted work as same-cycle Valid events where
the FSM cannot backpressure it further. S2 maps demands and hints into lookup
flows with demand-first arbitration. Drop hints unless they can win and enter
S3 immediately, before `to_decoupled()` checks acceptance; never buffer them.
Keep early virtual SRAM indexing independent of that token arbitration and
keep request-queue ingress readiness structural.

Event lineage follows the intrinsic queue/elastic/fixed-latency contracts of
the named service queue, S3 pipe, and S4 result register; callers do not restate
them. S3 observes lookup advancement before its gate, with a stall companion;
S4 observes all admitted demand or prefetch resolutions one cycle later.
Direct refill acceptance and command fields are captured on S4, before
arbitration with post-eviction work, not on a separate numbered stage.
S1/MEM and S2/WB observations belong to the core, where fast responses and
instruction identity are retained. The refill engine declares retained ownership
from command acceptance through completion; its request attempts carry S4
ancestry through the final arbiter. Explicitly detach post-eviction commands,
writeback requests, and maintenance requests until those owners are modeled.
Do not infer gather-FSM ancestry from an address or transaction ID. See the
parent [trace guide](../DEVELOPING.md#pipeline-event-annotations).

### Behavioral invariants

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
   authorized transaction path's S4/response registers into that hit path.
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
   S3 owns array reads, tag comparison, and word/state selection; its result
   crosses `ValidPipeAlwaysCapture` before S4 checks permissions and launches
   transactions. Reserve S4 capacity before advancing S3. Retain and reread
   younger S3 requests blocked by older S4 work or snoops; never retain their
   stale array results across mutation, refill, or coherence service. Include
   both stages in drain and maintenance ordering, but let snoops pass a retained
   S3 request once its outstanding read and S4 have drained.
3. Publish refill metadata only after the last word, and invalidate a dirty
   victim before reusing its way. Capture the cache's install disposition in
   transaction context separately from architectural locality and new-way
   allocation. A non-allocating load must complete from the acknowledged clean
   transaction buffer without SRAM writes, victim handling, replacement-pointer
   movement, or reservation invalidation.
4. Preserve explicit SRAM ownership and priority among core lookup, line
   gather, refill installation, and snoop service.
   Keep transaction capacity separate from array availability. `miss_active`,
   `miss_allows_hits`, and `miss_set` span direct acquisition or victim gather
   through final refill completion, including CHI retries and writeback.
   Only ordinary demand load/store contexts enable hit-under-miss. Protect the
   whole set in EX admission and MEM classification, and replay a second miss
   rather than exposing it as Slow service. Do not weaken store authorization,
   ordered-queue barriers, or `drained` when enabling independent load reads.
   Preserve registered read ownership across an installation or snoop arriving
   between EX and MEM; never steal an array cycle from older work.
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

For hit-under-miss changes, `rv5stage-dcache` also checks sustained independent
hits during delayed/retried refills and dirty writeback, both ways of a reserved
set, a second miss, blocked stores and atomics, gapped packet assembly,
installation progress, concurrent non-allocating completion, queued older
stores, invalidating snoops, and reset. Run `rv5stage-load-hit` at the real
core/MMU/router/cache boundary for hits during delayed load/store misses,
deferred-result use, and fence ordering. Include `rv5stage-dcache-rv32` and
`rv5stage-lrsc-progress` for width and coherence regression coverage, then rerun
SimpleSoC vvadd with the same ELF and host polling before claiming a speedup.

For internal flow changes, the RV64 cache bench also checks same-cycle demand
priority over a hint, hint drops during miss service, and retained younger
demand completion without a delayed hint transaction or duplicate response.
