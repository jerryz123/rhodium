<!-- Defines ownership, extension constraints, and validation for RTL graph materialization. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing program materialization

Read [`README.md`](README.md) for the public program and source-lifetime contracts.
[`program.rhm`](program.rhm) owns internal `materialize_reachable_rtl` and
post-copy certification. Core owns the shared `DesignElaboration(design, top)`
value used both before and after preparation.
It imports core IR, Builder, verification, and the local `copy.rhm` implementation.
The copier imports core IR and Builder.
The [package dependency contract](../DEVELOPING.md) keeps this package below
the frontend and independent of backend selection, analyses, and libraries.

The frontend owns active construction contexts and circuit recipes. It closes
its context before returning a program. Materialization owns the transition to
verified concrete RTL; frontend elaboration
does not invoke whole-program materialization. Direct Builder clients construct the same envelope without a
frontend context.

The materializer structurally verifies the complete source and checks combinational
cycles without sealing it, then copies the selected top closure into a fresh
destination. It returns a sealed, verified `DesignElaboration`. Module identity
controls copying reuse within one request; source names remain unchanged. DPI
imports are copied lazily. External live metadata references remain diagnostic.

[`copy.rhm`](copy.rhm) owns graph copying. It preallocates local values and
places to preserve forward references, remaps operation attributes/resources,
and rebuilds use-def and driver links. It finishes collection order but leaves
module shells open. Once every body
exists, a materialization-local resolver copies metadata and projected-place
views; only then does Builder finish each module and verification check the graph.
Concrete verification also seals the design. After verification, invoke
extension-owned `MaterializationCheck` payloads on the destination modules. This keeps analysis policy in its owner
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

[`tests/program-test.rhm`](tests/program-test.rhm) checks direct Builder
ownership, deferred whole-design verification, destination sealing, and source preservation.
[`../event/tests/materialized-metadata-test.rhm`](../event/tests/materialized-metadata-test.rhm)
checks Flow storage, shared controls, independent endpoint caches, diagrams,
event manifests, and instrumentation across shared ordinary children.
Frontend and backend program tests cover all four authoring paths and CIRCT
compilation. `make frontend-test` includes the lowering tests, and the compile
manifest includes them for CI.

Run focused tests through `tools/run-racket-tests.sh`; run
`make check-boundaries` after import or ownership changes. Shared elaboration
changes also require `make frontend-test lop-test` and the focused backend
program test, following the [test workflow](../../tools/testing/DEVELOPING.md).
