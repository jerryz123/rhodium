<!-- Defines ownership, extension constraints, and validation for portable program materialization. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing program materialization

Read [`README.md`](README.md) for the public phase and identity contracts.
[`program.rhm`](program.rhm) owns `ElaboratedProgram` and `materialize_rtl`.
It imports core IR, signatures, construct contracts, Builder, operation schemas,
verification, and dependency summaries, plus the local `copy.rhm` implementation.
The copier imports only core IR and Builder.
The [package dependency contract](../DEVELOPING.md) keeps this package below
the frontend and independent of backend selection, analyses, and libraries.

The frontend owns active construction contexts and circuit recipes. It closes
its context before returning a program. Materialization owns the transition to
verified concrete RTL; legacy elaboration calls it before returning. Direct
Builder clients construct the same envelope without a frontend context.

The materializer validates mixed source IR without sealing it, then copies the
whole module inventory into a fresh destination. Providers run with explicit
scratch Builders. Their module boundaries, pure-combinational contract, and
leaf dependencies are checked before recursive copying and checked again after
nested expansion. Only a fully concrete verified design can be returned.

[`copy.rhm`](copy.rhm) owns graph copying. It preallocates local values and
places to preserve forward references, remaps operation attributes/resources,
and rebuilds use-def and driver links. Metadata must implement the core
`remap_ir` protocol; the default is a diagnostic. The existing event-specific
copier has a different metadata policy and is not a dependency of this package.

Provider lookup and caches are local to a materialization. The active definition
stack rejects recursion and the expansion budget bounds changing-specialization
recursion. Definition identity, not display names or closure equality, controls
reuse. A provider exception never triggers a silent fallback.

[`tests/program-test.rhm`](tests/program-test.rhm) checks direct Builder
ownership, deferred whole-design verification, sealing, and identity.
Frontend and backend program tests cover compatibility through all four
authoring paths and unchanged CIRCT text. `make frontend-test` includes the
lowering tests, and the compile manifest includes them for CI.

Run focused tests through `tools/run-racket-tests.sh`; run
`make check-boundaries` after import or ownership changes. Shared elaboration
changes also require `make frontend-test lop-test` and the focused backend
program test, following the [test workflow](../../tools/testing/DEVELOPING.md).
