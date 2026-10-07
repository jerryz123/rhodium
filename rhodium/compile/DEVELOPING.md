<!-- Defines compile orchestration ownership, target preparation boundaries, and focused validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing target compilation

Read [README.md](README.md) for the public request, artifact, manifest, and
source-lifetime contracts. This package owns target-neutral compilation, not
source elaboration or any target's textual emitter.

## Architecture and ownership

Follow the [package dependency contract](../DEVELOPING.md). Compilation imports
core and portable lowering only. Backends opt in by importing these neutral
interfaces; the compiler never imports a target registry. The clock-analysis
target also imports compilation; internal analysis algorithms, core, lowering,
and frontend remain independent of it.

A target owns preparation and its `TargetPlan`. Keep that interface independent
of concrete RTL so native construct adapters can bypass portable expansion. The concrete helper uses the existing verifier and materializer;
it must not grow a second implementation of provider recursion, state checking,
or metadata copying.

## Implementation map

- `contracts.rhm`: immutable requests/results, occurrence decisions, and the
  target-owned plan protocol; imports public core descriptors only.
- `program.rhm`: invokes the explicit target and returns a complete result only
  after emission and artifact-set validation.
- `rtl.rhm`: shares `prepare_program` and occurrence reporting between mixed
  target preparation (`PreparedProgram`) and concrete `prepare_rtl`
  (`PreparedRTL`). Native decisions remain per occurrence. It asks lowering for a fresh reachable graph and maps its expansion
  provenance to a deterministic depth-first report. Traverse shared definitions
  per occurrence while retaining materialization's per-definition reuse.
  `rtl_target` exposes concrete graph inspection through ordinary compilation;
  its `RTLReport` owns the verified graph and it emits no text artifacts.
  `PreparedRTLConsumer` separates prepared-graph consumption from ordinary
  target preparation. `RTLTarget` implements it with one checked plan factory
  and mandatory portable RTL expansion; neither changes generic target policy.
- `pipeline.rhm`: ordered concrete instrumentation, instance-path preservation,
  physical manifests with source attribution, and stage-scoped
  findings. Imports core verification directly; it imports no instrumentation owner.
- `../analysis/clocking.rhm`: clock-analysis target, structured findings, and
  CDC errors/reconvergence warnings using the same concrete preparation helper.
- `../backend/circt-target.rhm`, `../backend/verilog-target.rhm`: concrete targets
  and plans using the existing internal emitters.

The lowering result maps construct definitions to destination modules. Derive
reports from this map and copied instance locations, never generated name
heuristics. A retained top uses an empty occurrence path. Artifact names and
module names belong to the selected target; file publication belongs to a
future explicit driver.

## Change workflow and validation

Add new target behavior to its owner. Keep new selection mechanisms tied to a
real supported backend rather than introducing placeholder emitters. Keep preparation fresh and scoped to the selected top. Keep generated
artifacts out of version control.

Concrete composition uses `PreparedRTLConsumer.plan(prepared)` after preparation. Do not
wrap an existing prepared graph in another `ElaboratedProgram` and recompile it:
that copies the graph and discards its construct-expansion report. Plan factories
retain the supplied graph and manifest without expanding, cloning, or emitting.
Use `rtl_pipeline_target` for multiple concrete instrumentation passes rather
than nesting target wrappers. The coordinator verifies each result and reconciles
its manifest before creating one final backend plan. Pass implementations own
semantic transparency and occurrence-aware copying of every metadata namespace;
stable hierarchy paths do not replace IR-object remapping. A later graph must preserve
prior observers and the identities recorded in their detached sidecars. Keep
live-IR reports explicitly stage-scoped rather than silently mixing graphs.

The supplied pass list defines execution order. Keep generic `CompileOptions`
unchanged: pass callbacks capture their own settings and validate their semantic
prerequisites. Keep this helper limited to instrumentation that preserves
existing paths and top ports. General compilation and retained-construct
interpretation still use `CompilationTarget`; they do not need to enter this RTL
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

`tests/rtl-test.rhm` checks fresh concrete/retained preparation, nested occurrence
paths, expansion reuse and limits, unused providers, and source preservation.
It also checks structured-only RTL compilation, rejection of plans with no
output, target-selected provider bypass, retained instance metadata, and
concrete-check fallback, including late provider checks and mixed-aware
certification. Ordinary graph consumers select `rtl_target`; direct `prepare_rtl`
calls belong to target implementation, prepared-graph composition, and tests
specifically exercising those boundaries.
`../backend/tests/compile-test.rhm` checks the real CIRCT target with Builder-owned concrete and retained
fixtures, mixed hierarchy, deterministic output, compatibility, and failures.
`../backend/tests/prepared-rtl-test.rhm` covers both real targets on concrete,
retained-top, and shared-child Builder programs. Provider and metadata-remap
counters distinguish ordinary preparation from graph reuse; exact artifacts,
manifest identity, and occurrence attribution protect the composition boundary.

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
Changes to portable materialization additionally need its host tests and shared
frontend/LOP regressions. Use existing native fixtures when emitted behavior
changes; exact emitter equality requires no new simulation model.
