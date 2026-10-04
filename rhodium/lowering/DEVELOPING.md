<!-- Defines ownership, extension constraints, and validation for portable program materialization. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing program materialization

Read [`README.md`](README.md) for the public program and source-lifetime contracts.
[`program.rhm`](program.rhm) owns `ElaboratedProgram` and internal `materialize_reachable_rtl`.
It imports core IR, signatures, construct contracts, Builder, operation schemas,
verification, and dependency summaries, plus the local `copy.rhm` implementation.
The copier imports core IR, retained-instance views, and Builder.
The [package dependency contract](../DEVELOPING.md) keeps this package below
the frontend and independent of backend selection, analyses, and libraries.

The frontend owns active construction contexts and circuit recipes. It closes
its context before returning a program. Materialization owns the transition to
verified concrete RTL; eager elaboration invokes it when constructs need
expansion. Direct Builder clients construct the same envelope without a
frontend context.

The materializer validates mixed source IR without sealing it, then copies the
selected top closure into a fresh destination. Providers run with explicit
scratch Builders. Their module boundaries, state/effect permissions, control
bindings, and leaf dependencies are checked before copying and again after
nested expansion. Only a fully concrete verified design can be returned.

[`copy.rhm`](copy.rhm) owns graph copying. It preallocates local values and
places to preserve forward references, remaps operation attributes/resources,
and rebuilds use-def and driver links. It finishes collection order before
checking expanded dependencies, but leaves module shells open. Once every body
exists, a materialization-local resolver copies metadata and projected-place
views; only then does Builder finish each module and verification seal the design.
After core verification, invoke extension-owned `MaterializationCheck` payloads
on the sealed destination modules. Eager frontend construction also runs these
checks after verifying its graph. This keeps analysis policy in its owner
without importing analysis or frontend modules into lowering. Failed checks must never return a result.

This ordering supports forward sibling references without attaching metadata to
finished modules or changing the core mutation contract.

Metadata and nested extension-owned objects implement the core `IRRemappable`
protocol; `ModuleMetadataPayload` extends it and its default is a diagnostic.
Memoization preserves shared instance/port views, arrays, and storage payloads.
Extensions retain their own immutable descriptors and remap their live fields;
the copier never imports frontend classes. Cyclic extension graphs are rejected.
The event-specific copier reuses the resolver with occurrence-specific maps and
reruns the same certification helper after instrumentation. Instance views select
the destination child through their copied operation, allowing shared definitions
to specialize without guessing which child module a bare reference denotes.
Dependency direction stays event-to-lowering; this package imports no event code.

`materialize_reachable_rtl` is the sole internal materialization primitive.
It uses the copier and checks with a fresh graph,
including for concrete input. Verify the complete source before selecting its
top closure. Copy only reachable modules and lazily remap DPI imports; reserve
names from reachable source modules so an unused name cannot perturb emission.
Return expansion provenance keyed by definition identity for compile reports.
Keep external live metadata references diagnostic rather than dropping metadata.

Provider lookup and caches are local to a materialization. The provider
environment merges validation callbacks separately from expansion identity.
Run merged checks after all metadata is remapped and concrete verification is
complete, including checks supplied by providers visited after a shared expansion.
The active definition stack rejects recursion and the expansion budget bounds changing-specialization
recursion. Definition identity, not display names or closure equality, controls
reuse. A detached retained top follows the same expansion path as a child;
concrete roots retain their existing ownership checks. The graph remapper turns
`ConstructInstance` metadata views into `Instance` views of the expanded operation. A provider exception never triggers a silent fallback.

[`tests/program-test.rhm`](tests/program-test.rhm) checks direct Builder
ownership, deferred whole-design verification, destination sealing, and source preservation.
[`../event/tests/retained-metadata-test.rhm`](../event/tests/retained-metadata-test.rhm)
checks Flow storage, shared controls, independent endpoint caches, diagrams,
event manifests, and instrumentation beside a retained combinational child.
Frontend and backend program tests cover all four authoring paths and CIRCT
compilation. `make frontend-test` includes the lowering tests, and the compile
manifest includes them for CI.

[`tests/retained-state-test.rhm`](tests/retained-state-test.rhm) checks explicit
Builder state providers, transitive clock/reset bindings, dependency refinement,
and certification ordering. [`tests/retained-memory-test.rhm`](tests/retained-memory-test.rhm)
checks independent storage/assertion permissions, nested retained permission
containment, per-occurrence clock/reset binding, read-address dependencies, and
assertion attribution. `check_implementation_contract` handles every admitted
memory opcode before the generic combinational category, so asynchronous reads
cannot bypass permission checks. Check nested permissions before provider
execution and actual effects again after expansion. Keep other effects rejected
until their contracts are introduced. Reset recognition covers structural aliases,
one-bit casts, and OR-extension rather than general Boolean equivalence.

Run focused tests through `tools/run-racket-tests.sh`; run
`make check-boundaries` after import or ownership changes. Shared elaboration
changes also require `make frontend-test lop-test` and the focused backend
program test, following the [test workflow](../../tools/testing/DEVELOPING.md).
