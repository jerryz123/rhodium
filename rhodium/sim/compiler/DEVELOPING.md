<!-- Owns graph-pass ordering, proof boundaries, serialization, and compiler validation. -->

# Developing the simulation compiler

Read the [exchange and option contract](README.md) and
[parent architecture](../DEVELOPING.md). This compiler owns a private IR and
must not depend on frontend or circuit-library implementation packages.

| Sources | Responsibility |
|---|---|
| `model.*`, `semantic.cpp`, `kernel.hpp` | Exact-width values, JSON/binary validation, demanded structural lowering and local programs |
| `passes.cpp`, `regroup.cpp`, `bit-relations.cpp` | Simplification, word recovery, Boolean packing and bounded equivalence proofs |
| `contracts.cpp`, `flow.cpp`, `selectors.cpp` | Pure contract programs, handshake/matcher structure and selector matrices |
| `fifo-width.cpp`, `fifo-derived.cpp` | Inductive payload widths and enqueue-time derived fields |
| `lifetimes.cpp`, `transport.cpp`, `payloads.cpp` | Proven payload lifetimes, routed ownership and general pool grouping |
| `replicate.cpp`, `snapshot.cpp` | Bounded pure duplication and snapshot analysis |
| `main.cpp` | Pass order, validation checkpoints, reports and timings |

## Pass invariants

Every result has a declared width and one definition. Constant arithmetic uses
arbitrary precision and truncates to that width. Structural lowering memoizes
demanded leaves; a whole-aggregate cycle is not a reason to reject independently
acyclic fields. Unknown producers remain conservative.

The ordinary sequence shares decoders/matcher/TLB queries, simplifies, recovers
words, regroups Boolean lanes and simplifies again. Optional passes and release
cleanup follow the order in `main.cpp`. Validate after every transformation and
restore topology after inserting new dependencies. Remap all surviving state,
effect, port and metadata bindings; keep pass-input IDs explicitly distinguished
from final IDs in diagnostic reports.

Liveness roots include effects, state and strict partial operators. Release
cleanup removes diagnostic-only roots under the documented preconditions.
Register-update fusion preserves enables, reset/hold alternatives and index
widths. Regrouping follows compatible word slices, common predicates and mux
selectors without expanding arbitrary Boolean graphs; bounds or unfavorable
cost estimates leave the original graph intact.

### Contract programs and demand

Only closed, total, pure cones can enter a program. Trim escaping intermediates
and preserve each output's real dependencies; unrelated outputs must not acquire
false combinational feedback. Place a value at the least common ancestor of its
consumer branch scopes. Shared selectors and effects cannot be deferred merely
because one output is inactive. Keep generated temporary and store provenance
consistent with `specialize-empty.cpp` and its `rds-region-end` boundaries.

Program limits and supported operations are shared with
`../runtime/kernel-format.h`. Layout and emission must agree on cache eligibility;
cache keys include all significant input bits and initialization state. Imported
or replaced compiled state must invalidate derived caches. One-bit arithmetic can
reduce modulo two, but wider results retain their full arithmetic semantics.

### Payload ownership

A lifetime proof must cover enqueue, retention, transfer, dequeue, reset and every
observer. Inspect actual dependencies rather than module names or sampled traffic.
Preserve control fields used during transit; terminal reconstruction must retain
original bit order. Pool reads must not be reintroduced into readiness, validity,
routing or allocation cones.

For routed exchange, prove unique transfer/departure, no unsupported fanout and
safe reuse of every old-state handle. Proof failure leaves the candidate unchanged.
Packed handles cannot cross word boundaries. Bitmap allocation uses old free
capacity and publishes frees at the edge. SRAM addresses remain stable while
handles and control state change. General pool grouping has distinct invalid-slot
lifetime requirements and must not borrow the stronger opaque-payload assumptions.

### FIFO and matcher rewrites

FIFO width inference computes an ascending may-one fixed point including feedback
and reset-edge writes. Unsupported state is unknown. Narrowing restores original
query widths explicitly. Derived fields are total functions of one FIFO payload;
encoding against `f(0)` preserves initially zero storage and invalid previews.

Matcher reuse requires equality with the original update requests and the exact
prefix exclusion proof. Column splitting keeps independent priority owners.
Handshake sharing is a word identity for arbitrary masks; it does not assume
one-hot grants or alter accepted transfers. Decoder rewrites preserve priority,
masked keys and defaults over the entire selector domain.

## Validation and performance

Use [tests/sim](../../../tests/sim/README.md). Each pass needs original-versus-
optimized execution and an independent oracle where practical, including supported
invalid inputs and strict/release behavior. Contract changes also run generated
one/multi-worker and reattachment checks. Ownership changes require failed-host,
reset, preview and intermediate-reader coverage. Format changes require round trips
and malformed-input checks across compiler and runtime.

Retained fixtures support offline iteration. A full extraction/runtime change uses
`make sim-test`; Verilator differential checks are separate. Build sanitizers into
the optimizer separately from runtime sanitizer flags. Keep generated fixtures,
profiles, benchmark logs and intermediate snapshots outside version control.

Measure optimizer wall time separately from elaboration, C generation, host
compilation and cycle throughput. Count inner contract work as well as outer graph
nodes. Optional transformations need workload-specific timing; operation-count
reductions alone do not establish a speedup.
