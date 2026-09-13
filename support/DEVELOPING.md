<!-- Explains how to place, implement, and validate dependency-neutral shared Rhombus refinements. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing shared refinements

Read the package [README](README.md) for the public annotations and the decision
rule for choosing one. This guide owns their implementation and maintenance.

## Boundaries and placement

Keep this package below its consumers: it must not import Rhodium, NoC, RISC-V,
CHI, devices, SoCs, backends, or tool integrations. Its implementation may use
ordinary Rhombus libraries but must remain independent of hardware semantics.

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

## Implementation map

| Path | Role |
|---|---|
| [`annotations.rhm`](annotations.rhm) | Public exports plus the private predicates and ASCII identifier pattern |
| [`tests/annotations-test.rhm`](tests/annotations-test.rhm) | Direct acceptance and rejection checks |
| [`../Makefile`](../Makefile) | Discovers `support/tests/*-test.rhm` and exposes `support-annotation-test` |
| [`../tools/check-boundaries.sh`](../tools/check-boundaries.sh) | Enforces the dependency-neutral import boundary |

## Focused validation

From the repository root, run:

```sh
make support-annotation-test
make check-boundaries
```

The test wrapper supplies a fresh `PLTCOMPILEDROOTS` when the caller has not
provided one. The direct suite covers valid nonempty text, empty-string
rejection, a valid ASCII identifier, and leading-digit and hyphenated
identifier rejections. Run broader consumer tests only when the annotation's
accepted values or a consumer boundary changes.
