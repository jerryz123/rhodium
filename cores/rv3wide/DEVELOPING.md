<!-- Defines the experimental RV3Wide facade's ownership and shared validation route. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RV3Wide

Read [README.md](README.md) for the current two-wide contract and
[the superscalar guide](../superscalar/DEVELOPING.md) for the implementation.

This directory owns a distinct `RV3WideConfig` and `RV3WideHart`. The config
contains a `SuperscalarPipelineConfig` in `pipeline`; its
architectural/CSR descriptions, direct core factories, hart, and UDB must use
that same value. Shared parameter defaults and validation belong in
`superscalar/profile.rhm`, not duplicated constructors or forwarding fields here.
The hart independently owns reset/start and CHI wiring.
Do not copy decode or create an independent pipeline. Keep existing two-lane
logic unchanged until a separate width-extension change is designed and validated.

The shared profile tests cover configuration and UDB agreement. The existing
`rv2wide-fetch-disabled` behavioral fixture uses `RV3Wide`, so it tests real
instruction execution, recovery, caches, and translation without a duplicate
oracle or new CI lane. Preserve that fixture ID as the established baseline.
SoC profile/binding adapters live in `socs/configs/rv3wide-core-profile.rhm` and
`socs/harts/rv3wide.rhdl`. Keep CI shape/ISA enrollment paired with RV2Wide;
software policy remains shape/ISA-owned.
Follow the shared guide for focused commands and run `moon run rhodium:check-boundaries`
when changing package dependencies.
