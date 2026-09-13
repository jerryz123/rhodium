<!-- Explains how to extend and validate backend-independent Rhodium analyses. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing Rhodium analysis

Read the [analysis guide](README.md) for the public clocking, temporal
provenance, environment, CDC, and reconvergence contracts. This guide maps
those contracts to their implementation and test ownership.

## Architecture and boundaries

Analysis consumes completed, verified core IR and returns derived facts. It
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

Frontend declarations construct environments, core verification owns crossing
evidence invariants, and the backend owns any emitted attributes. Do not make
an analysis report a prerequisite for CIRCT lowering.

## Implementation map

| Path | Responsibility |
|---|---|
| [`clocking.rhm`](clocking.rhm) | Stable public re-export surface |
| [`clocking/types.rhm`](clocking/types.rhm) | Result, environment, provenance, classification, violation, and reconvergence objects |
| [`clocking/module.rhm`](clocking/module.rhm) | Clock/reset inventory and reusable hierarchy-aware provenance |
| [`clocking/environment.rhm`](clocking/environment.rhm) | Boundary validation, classification resolution, CDC violations, and strict policy |
| [`../frontend/support/clocking.rhm`](../frontend/support/clocking.rhm) | Ambient synchronous-circuit expansion and single-clock certification consumer |
| [`../frontend/layers/clocking.rhm`](../frontend/layers/clocking.rhm) | Author declarations and elaboration wrappers |
| [`../core/verify.rhm`](../core/verify.rhm) | Structural crossing-evidence invariants |
| [`../backend/circt.rhm`](../backend/circt.rhm) | Metadata omission and `async_reg` emission |

## Change an analysis

When adding a derived fact, decide whether it is module-reusable or requires a
closed top environment. Preserve aggregate leaf paths, instance paths, stable
operation identity, and deterministic ordering in every structured result.
Readable reports should be projections of structured summaries rather than a
second analysis path.

When changing CDC policy, keep raw provenance and classification available to
report-only consumers. Add a violation only when the policy can name the exact
sink, leaf, origin, and reason. Do not turn protocol coherency, reset-domain
crossing, physical placement, or MTBF assumptions into clock-domain facts.

Update the public README whenever result shape, classification, enforcement, or
deliberate limits change. A source-only refactor that preserves those contracts
belongs only here.

## Focused validation

The focused ownership is:

- `clocking-test.rhm` for clock/reset inventories, aliases, and certification;
- `clocking-provenance-test.rhm` for leaf-sensitive hierarchy provenance;
- `clocking-environment-test.rhm` for boundary facts and invalid environments;
- `clocking-cdc-test.rhm` for violations, crossings, lineage, and reconvergence;
- core `cdc-test.rhm` and backend `cdc-test.rhm` for the evidence and attribute
  handoff boundaries.

Run the analysis batch through the repository wrapper:

```sh
env -u PLTCOMPILEDROOTS tools/run-racket-tests.sh tests/analysis/*-test.rhm
```

The wrapper creates a fresh compiled root when none is supplied. Run broader
frontend or backend checks only when their owned side of the integration
changes.
