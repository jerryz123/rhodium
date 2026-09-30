<!-- Explains how to extend and validate backend-independent Rhodium analyses. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing Rhodium analysis

Read the [analysis guide](README.md) for the public clocking, temporal
provenance, environment, CDC, and reconvergence contracts. This guide maps
those contracts to their implementation and test ownership.

## Architecture and boundaries

The public target adapter prepares concrete RTL through neutral compilation.
Internal analysis consumes that verified core IR and returns derived facts. It
must not mutate hardware, define authoring syntax, or participate in backend
lowering. Optional policy belongs here only when it can be derived from the
public IR without changing universal hardware meaning.

Keep the stages distinct:

1. Module analysis inventories explicit clocks/resets and produces reusable,
   symbolic leaf provenance.
2. Environment resolution validates top-boundary timing declarations and
   resolves symbolic origins in one selected design.
3. CDC enforcement interprets the resolved summary under the current policy.
4. Reconvergence remains a diagnostic over verified crossing identities rather
   than an automatic violation.

Frontend declarations store remappable environment metadata. Core verification owns crossing
evidence invariants, and the backend owns any emitted attributes. Do not make
an analysis report a prerequisite for CIRCT lowering.

## Implementation map

| Path | Responsibility |
|---|---|
| [`clocking.rhm`](clocking.rhm) | Sole public execution surface: target factory, target-owned plan/report, and data exports |
| [`clocking/declarations.rhm`](clocking/declarations.rhm) | Environment metadata attachment, collection, and explicit reference remapping |
| [`clocking/types.rhm`](clocking/types.rhm) | Result, environment, provenance, classification, violation, and reconvergence objects |
| [`clocking/module.rhm`](clocking/module.rhm) | Clock/reset inventory and reusable hierarchy-aware provenance |
| [`clocking/environment.rhm`](clocking/environment.rhm) | Boundary validation, classification resolution, CDC violations, and strict policy |
| [`../frontend/support/clocking.rhm`](../frontend/support/clocking.rhm) | Ambient synchronous-circuit expansion and single-clock certification consumer |
| [`../frontend/layers/clocking.rhm`](../frontend/layers/clocking.rhm) | Author declarations and durable crossing evidence |
| [`../core/verify.rhm`](../core/verify.rhm) | Structural crossing-evidence invariants |
| [`../backend/circt.rhm`](../backend/circt.rhm) | Metadata omission and `async_reg` emission |

Only `clocking.rhm` may import neutral compilation contracts and RTL preparation.
Implementation modules under `clocking/` depend on core and sibling analysis
modules only. Frontend clock certification imports the internal module helper;
frontend declarations import data/metadata modules, never the target adapter.
The compiler does not import analysis or register targets globally.

## Change an analysis

Clock-use inventory reads `ConstructDefinition` state contracts for retained
instances. Only that boundary-level inventory accepts mixed IR; temporal
provenance and CDC still require concrete modules. Keep retained control binding
in `operation_clock_use` so summary reconstruction and certification agree.

When adding a derived fact, decide whether it is module-reusable or requires a
closed top environment. Preserve aggregate leaf paths, instance paths, stable
operation identity, and deterministic ordering in every structured result.
Readable reports should be projections of structured summaries rather than a
second analysis path. Return findings through `CompilationResult.report`; do not
add direct public runners or specialized elaboration wrappers. An optional target
environment factory must bind to the prepared top, and its declarations augment
remapped program metadata. Validate all timing assumptions before returning a result.

When changing CDC policy, keep raw provenance and classification available to
report-only consumers. Add a violation only when the policy can name the exact
sink, leaf, origin, and reason. Do not turn protocol coherency, reset-domain
crossing, physical placement, or MTBF assumptions into clock-domain facts.

Update the public README whenever result shape, classification, enforcement, or
deliberate limits change. A source-only refactor that preserves those contracts
belongs only here.

## Focused validation

The focused ownership is:

- `clocking-target-test.rhm` for compilation, source preservation, declaration
  remapping, retained expansion, strict CDC failure, and explicit environments;
- `clocking-test.rhm` for internal clock/reset inventories, aliases, and certification;
- `clocking-provenance-test.rhm` for leaf-sensitive hierarchy provenance;
- `clocking-environment-test.rhm` for boundary facts and invalid environments;
- `clocking-cdc-test.rhm` for violations, crossings, lineage, and reconvergence;
- core `cdc-test.rhm` and backend `cdc-test.rhm` for the evidence and attribute
  handoff boundaries.

Run the analysis batch through the repository wrapper:

```sh
tools/run-racket-tests.sh rhodium/analysis/tests/*-test.rhm
```

The wrapper selects the persistent worktree-specific root when none is supplied. Run broader
frontend or backend checks only when their owned side of the integration
changes.
