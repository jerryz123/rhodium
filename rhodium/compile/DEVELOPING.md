<!-- Defines compile orchestration ownership, target preparation boundaries, and focused validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing target compilation

Read [README.md](README.md) for the public request, artifact, manifest, and
source-lifetime contracts. This package owns target-neutral compilation, not
source elaboration or any target's textual emitter.

## Architecture and ownership

Follow the [package dependency contract](../DEVELOPING.md). Compilation imports
core and graph materialization only. `compile_program` accepts the core
`DesignElaboration` directly; only RTL preparation imports the materializer. Backends opt in by importing these neutral
interfaces; the compiler never imports a target registry. The clock-analysis
target also imports compilation; internal analysis algorithms, core, lowering,
and frontend remain independent of it.

A target owns preparation and its `TargetPlan`. Keep that interface independent
of concrete RTL. The RTL helper uses the existing verifier and materializer;
it must not grow a second implementation of verification or metadata copying.

## Implementation map

- `contracts.rhm`: immutable results, boundary manifests, and the
  target-owned plan protocol; imports public core descriptors only.
- `program.rhm`: invokes the explicit target and returns a complete result only
  after emission and artifact-set validation.
- `rtl.rhm`: prepares a fresh verified reachable graph and matching manifest.
  `rtl_target` exposes that graph through `RTLReport` without text artifacts.
  `RTLTarget` shares one checked plan factory across ordinary preparation and
  `.plan(prepared)` graph reuse, including both rsim configurations.
- `pipeline.rhm`: ordered concrete instrumentation, instance-path preservation,
  physical manifests with source attribution, and stage-scoped
  findings. Imports core verification directly; it imports no instrumentation owner.
- `../analysis/clocking.rhm`: clock-analysis target, structured findings, and
  CDC errors/reconvergence warnings using the same concrete preparation helper.
- `../backend/circt-target.rhm`, `../backend/verilog-target.rhm`: concrete targets
  and plans using the existing internal emitters.

## Change workflow and validation

Add new target behavior to its owner. Keep new selection mechanisms tied to a
real supported backend rather than introducing placeholder emitters. Keep preparation fresh and scoped to the selected top. Keep generated
artifacts out of version control.

Concrete composition uses `RTLTarget.plan(prepared)` after preparation. Do not
pass an already prepared graph back through `compile_program`:
that repeats copying and certification. Plan factories retain the supplied graph
and manifest without cloning or emitting.
Use `rtl_pipeline_target` for multiple concrete instrumentation passes rather
than nesting target wrappers. The coordinator verifies each result and reconciles
its manifest before creating one final backend plan. Pass implementations own
semantic transparency and occurrence-aware copying of every metadata namespace;
stable hierarchy paths do not replace IR-object remapping. A later graph must preserve
prior observers and the identities recorded in their detached sidecars. Keep
live-IR reports explicitly stage-scoped rather than silently mixing graphs.

The supplied pass list defines execution order. Pass callbacks capture their
own settings and validate their semantic
prerequisites. Keep this helper limited to instrumentation that preserves
existing paths and top ports. General compilation uses `CompilationTarget`
without needing to enter this RTL
pipeline. Prefer existing Builder, metadata-remapping, verifier, artifact, and
report contracts to new feature-specific protocols. Add machinery only when an
implemented consumer needs it.

Collect each plan projection once, including diagnostics. Require artifacts or
a structured report; graph-only targets must not fabricate text artifacts just
to satisfy result validation. Error-severity findings
must not suppress completed artifacts; enforcement belongs in the invoking
workflow. Keep `has_errors` derived from diagnostics and preserve exception
propagation for failures that prevent a trustworthy result. Generic diagnostics
contain detached locations and instance-name paths; target reports own live
prepared-graph references. Test diagnostic projection failures using the real
CIRCT plan's existing failure-injection coverage.

`tests/rtl-test.rhm` checks nested hierarchy, closure selection, source preservation,
structured-only results, and invalid graph/output diagnostics.
`../backend/tests/compile-test.rhm` checks real CIRCT artifacts and failure atomicity.
`../backend/tests/prepared-rtl-test.rhm` checks both RTL emitters on ordinary roots
and shared children. Metadata-remapping counters distinguish preparation from
plan reuse; exact artifacts and manifest identity protect the composition boundary.

`../backend/tests/rtl-pipeline-test.rhm` exercises two ordered transformations
through CIRCT, direct SV, and rsim plans, including per-occurrence specialization,
metadata survival, source preservation, provenance, sidecars, explicit ordering,
and failed compilation. It belongs with backend integration because it imports
the emission targets; production compile code remains independent of them.

```sh
tools/run-racket-tests.sh rhodium/compile/tests/rtl-test.rhm rhodium/backend/tests/compile-test.rhm rhodium/backend/tests/prepared-rtl-test.rhm rhodium/backend/tests/rtl-pipeline-test.rhm
make backend-test check-boundaries ci-plan-test
```

The backend host target and shared compile manifest include this package's tests.
Changes to graph materialization additionally need its host tests and shared
frontend/LOP regressions. Use existing native fixtures when emitted behavior
changes; exact emitter equality requires no new simulation model.
