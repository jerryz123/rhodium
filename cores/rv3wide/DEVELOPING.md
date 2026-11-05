<!-- Defines the experimental RV3Wide facade's ownership and shared validation route. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RV3Wide

Read [README.md](README.md) for the current three-lane integer contract and
[the superscalar guide](../superscalar/DEVELOPING.md) for the implementation.

This directory owns a distinct `RV3WideConfig` and `RV3WideHart`. The config
contains a `SuperscalarPipelineConfig` in `pipeline`; its
architectural/CSR descriptions, direct core factories, hart, and UDB must use
that same value. Shared parameter defaults and validation belong in
`superscalar/profile.rhm`, not duplicated constructors or forwarding fields here.
The hart independently owns reset/start and CHI wiring and supplies
`RV3WideIssue(xlen)` through `~issue`. `issue.rhdl` owns the three-candidate
prefix, bounded read borrowing, and last-lane completion reservations. Shared contracts
and predicates remain under superscalar; neither family imports this policy.
Do not copy decode or create an independent pipeline. Shared execution derives
its lane count from the supplied issue signature. Service selectors, FP ownership,
vector descriptors, and predictor training cover every lane. GPR storage has
four read ports and three fixed write ports; shared service bandwidth is unchanged.

The shared profile tests cover configuration and UDB agreement. The existing
`rv2wide-fetch-disabled` behavioral fixture uses `RV3Wide`, so it tests real
instruction execution, recovery, caches, and translation without a duplicate
oracle or new CI lane. Preserve that fixture ID as the established baseline.
SoC profile/binding adapters live in `socs/configs/rv3wide-core-profile.rhm` and
`socs/harts/rv3wide.rhdl`. Keep CI shape/ISA enrollment paired with RV2Wide;
software policy remains shape/ISA-owned.
Follow the shared guide for focused commands and run `moon run rhodium:check-boundaries`
when changing package dependencies.

Run `python3 tools/testing/rsim/run.py --fixture rv3wide-issue --fixture rv3wide-issue-rv32`
for standalone three-candidate admission changes. These fixtures share the
common issue oracle and add third-candidate checks. The integrated backend
supplies three candidates. Run `rv3wide-core` for three retirements, borrowed
operands, three independent fixed write ports, shortened fault/replay prefixes,
RF consumers, and PC-region crossings. Pair it with `rv2wide-core`, FP/vector
integration, and the existing production `rv2wide-fetch-disabled` fixture.
Run `rv3wide-services` for third-lane load/store hits, deferred returns and replay,
all-older fault suppression, branches and ordered training, fixed/variable FP,
multiply/divide, vector configuration and retained vector memory. It also checks
two ordinary writes alongside a delayed load, multiply, FP, or vector GPR return.
