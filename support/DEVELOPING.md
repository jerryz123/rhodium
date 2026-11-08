<!-- Explains ownership and validation of pure host refinements and hardware-data descriptions. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing shared host descriptions

Read the package [README](README.md) for the public annotations, shared enums, and the decision
rule for choosing one. This guide owns their implementation and maintenance.

## Boundaries and placement

Keep this package below its consumers: it must not import Rhodium, NoC, RISC-V,
CHI, devices, SoCs, backends, or tool integrations. Its implementation may use
ordinary Rhombus libraries but must remain independent of IR construction and
hardware elaboration.

Add a refinement here only when it constrains an ordinary host value, is useful
to unrelated packages, and does not encode domain vocabulary. Use built-in
Rhombus annotations directly instead of wrapping them. Keep relationships among
values, uniqueness, protocol legality, IR validity, and generated-name policy
with their semantic owner.

Do not confuse this package with
[`../rhodium/frontend/support/`](../rhodium/frontend/support/), which owns
frontend macro and static-information machinery and may depend on Rhodium
implementation packages.

## Add or change an annotation

1. Define the narrow predicate and annotation macro in
   [`annotations.rhm`](annotations.rhm).
2. Keep the public name and accepted value set documented in
   [README.md](README.md#public-annotations).
3. Test acceptance and each meaningful rejection boundary directly. Consumer
   suites need only test enforcement where it forms part of their API.
4. Run package-boundary checks if imports change, and update consumers only
   when the public constraint changes.

Avoid collecting aliases for built-in annotations or helpers used by only one
domain. A broadly named annotation should not hide a protocol- or
implementation-specific policy.

## Shared data descriptions

`hardware-data.rhm` owns the small type, literal, finite-enum, and selector
protocols shared with core and frontend consumers. These are immutable host
descriptions: they must not allocate IR or invoke an elaboration context.
Core retains structural type validation and all operations. Frontend kernel
materializes `HardwareLiteral` values as packed constants and explicit casts.

`hardware-enum.rhm` defines the single public enum declaration; `variants.rhm`
owns its nominal descriptor, validation, and immutable member values, also used
by tagged unions. Preserve nominal equality and hashing in host maps/sets.
The declaration and annotation macros consult lexical authoring adapters when
present, never a mutable global registry. Rhodium supplies the richer hardware
method/annotation surface; importing a host catalog must not import Rhodium.
Descriptor encoding-family metadata must survive aliases without changing
the descriptor's identity. Test both host-only use and RTL imports when changing
this boundary.

## Implementation map

| Path | Role |
|---|---|
| [`annotations.rhm`](annotations.rhm) | Public exports plus the private predicates and ASCII identifier pattern |
| [`hardware-data.rhm`](hardware-data.rhm) | Pure type, literal, and selector protocols |
| [`hardware-enum.rhm`](hardware-enum.rhm), [`variants.rhm`](variants.rhm) | Shared enum declaration, immutable schemas and nominal member values |
| [`tests/annotations-test.rhm`](tests/annotations-test.rhm) | Direct acceptance and rejection checks |
| [`tests/hardware-enum-test.rhm`](tests/hardware-enum-test.rhm) | Pure-host enum identity, encoding, annotation, and hashing checks |
| [Moon tests](../.moon/tasks/tests.yml) | Discovers `support/tests/*-test.rhm` and exposes `support-annotation-test` |
| [`../tools/check-boundaries.sh`](../tools/check-boundaries.sh) | Enforces the dependency-neutral import boundary |

## Focused validation

From the repository root, run:

```sh
tools/run-racket-tests.sh support/tests/hardware-enum-test.rhm
python3 tools/testing/rsim/run.py --fixture host-enum
moon run rhodium:support-annotation-test
moon run rhodium:check-boundaries
```

The test wrapper manages the persistent worktree-specific compiled root. The
host suites cover annotation boundaries and enum identity, hashing, encodings,
and invalid widths. The `host-enum` simulation checks imported enum constants
and signals through registers, bundles, vectors, instance ports, and one-hot
selection. Run broader consumer tests when accepted values or a consumer
boundary changes.
