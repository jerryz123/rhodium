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
of concrete RTL so direct construct adapters can eventually bypass portable
expansion. The concrete helper uses the existing verifier and materializer;
it must not grow a second implementation of provider recursion, state checking,
or metadata copying.

## Implementation map

- `contracts.rhm`: immutable requests/results, occurrence decisions, and the
  target-owned plan protocol; imports public core descriptors only.
- `program.rhm`: invokes the explicit target and returns a complete result only
  after emission and artifact-set validation.
- `rtl.rhm`: asks lowering for a fresh reachable graph and maps its expansion
  provenance to a deterministic depth-first report. Traverse shared definitions
  per occurrence while retaining materialization's per-definition reuse.
  `RTLTarget` shares one checked plan factory between ordinary preparation and
  the prepared-graph entry point; neither path changes generic target policy.
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

Concrete composition uses `RTLTarget.plan(prepared)` after preparation. Do not
wrap an existing prepared graph in another `ElaboratedProgram` and recompile it:
that copies the graph and discards its construct-expansion report. Plan factories
retain the supplied graph and manifest without expanding, cloning, or emitting.
A transformation owns verification of its output and reconciliation of its
physical manifest with occurrence provenance before constructing an emission plan.

Collect each plan projection once, including diagnostics. Error-severity findings
must not suppress completed artifacts; enforcement belongs in the invoking
workflow. Keep `has_errors` derived from diagnostics and preserve exception
propagation for failures that prevent a trustworthy result. Generic diagnostics
contain detached locations and instance-name paths; target reports own live
prepared-graph references. Test diagnostic projection failures using the real
CIRCT plan's existing failure-injection coverage.

`tests/rtl-test.rhm` checks fresh concrete/retained preparation, nested occurrence
paths, expansion reuse and limits, unused providers, and source preservation.
`../backend/tests/compile-test.rhm` checks the real CIRCT target with Builder-owned concrete and retained
fixtures, mixed hierarchy, deterministic output, compatibility, and failures.
`../backend/tests/prepared-rtl-test.rhm` covers both real targets on concrete,
retained-top, and shared-child Builder programs. Provider and metadata-remap
counters distinguish ordinary preparation from graph reuse; exact artifacts,
manifest identity, and occurrence attribution protect the composition boundary.

```sh
tools/run-racket-tests.sh rhodium/compile/tests/rtl-test.rhm rhodium/backend/tests/compile-test.rhm rhodium/backend/tests/prepared-rtl-test.rhm
make backend-test check-boundaries ci-plan-test
```

The backend host target and shared compile manifest include this package's tests.
Changes to portable materialization additionally need its host tests and shared
frontend/LOP regressions. Use existing native fixtures when emitted behavior
changes; exact emitter equality requires no new simulation model.
