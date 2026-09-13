<!-- Explains RFPL's implementation boundary, extension workflow, and focused validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RFPL

Read the RFPL [README](README.md) for the public physical-view model,
construction rules, validation behavior, and deliberate limits. This guide
owns implementation structure and contributor workflow.

## Architecture and package boundary

RFPL reads completed public Rhodium core IR. It must not author or mutate
modules, ports, instances, drives, operations, resources, or logical hierarchy.
Rhodium core, frontend, standard libraries, analyses, and backends must not
import RFPL.

`FloorplanDesign` retains the original `DesignElaboration` and adds a validated
view tree. The existing backend continues to receive only the original logical
`Design`; no outline, coordinate, placement, or view identity may enter CIRCT
or generated RTL.

Keep physical validation local to the view model: dimensions, coordinates,
containment, target identity, composite completeness, and common logical-design
ownership. Routing, overlap, timing, PDN, physical export, and signoff remain
outside the implemented contract until a dedicated downstream stage owns them.

## Implementation map

| Concern | Owner |
|---|---|
| `#lang rfpl` reader shim | [`main.rkt`](main.rkt) |
| Ordinary Rhombus plus RFPL exports | [`language.rhm`](language.rhm) |
| Public classes, units, lookup, view construction, placement, and annotation traversal | [`frontend/foundation.rhm`](frontend/foundation.rhm) |
| Wiring-only composite validation | [`frontend/verify.rhm`](frontend/verify.rhm) |
| Dependency and file-extension policy | [`check-boundaries.sh`](check-boundaries.sh) |
| Implemented status and deferred direction | [`PLAN.md`](PLAN.md) |
| Structural and CIRCT-isolation checks | [`tests/structural-test.rhm`](tests/structural-test.rhm) |
| Rejected authoring cases | [`tests/invalid/`](tests/invalid/) and [`tests/run-negative-cases.rktd`](tests/run-negative-cases.rktd) |
| Canonical logical/physical pair | [`../examples/rfpl/`](../examples/rfpl/) |

## Extend the model

1. Decide whether the feature annotates existing logical objects or requires a
   new physical downstream representation. Do not put logical construction in
   RFPL.
2. Define the public object and authoring form in `frontend/foundation.rhm` and
   its observable contract in [README.md](README.md).
3. Add construction-local checks near the form and cross-view/hierarchy checks
   during annotation traversal.
4. Preserve exact logical module and instance identity; do not reconstruct
   hierarchy from display names, module order, or transient numeric IDs.
5. Add valid structural coverage and intentional-invalid diagnostics.
6. Reconfirm that emitting the retained logical design produces no RFPL
   metadata or RTL change.

Future physical export or place-and-route integration requires its own explicit
artifact and ownership boundary; it must not be smuggled through logical IR
metadata.

## Focused validation

From the repository root, run:

```sh
make rfpl-test
make rfpl-circt-test
```

The first target runs RFPL boundaries, structural checks, invalid fixtures, and
examples. The second runs the RFPL-owned CIRCT fixture and compares its
example-owned normalized Verilog reference. It requires `circt-opt`; run
`make setup-circt` or set `CIRCT_OPT` when needed. Repository wrappers provide
a fresh `PLTCOMPILEDROOTS` unless the caller supplies one.
