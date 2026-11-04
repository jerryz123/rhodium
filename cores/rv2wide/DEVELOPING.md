<!-- Routes RV2Wide maintenance to the single shared superscalar implementation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RV2Wide

Read [README.md](README.md) for the named API and
[the superscalar guide](../superscalar/DEVELOPING.md) for pipeline architecture,
source ownership, and focused validation.

This directory owns the `RV2WideConfig` wrapper and `RV2WideHart` reset/start and
CHI wiring. The wrapper contains one `SuperscalarPipelineConfig` in `pipeline`;
shared parameters, defaults, and validation belong in `superscalar/profile.rhm`.
Keep only genuinely named-core policy here, without copying pipeline fields or
adding private decode, frontend, execution, memory, or retirement implementations.
Architecture/CSR descriptions delegate to the contained config.
Neither RV2Wide nor RV3Wide imports the other; both depend on
`cores/superscalar/`. Existing SoC profile selection remains in `socs/configs/`.

Keep core factories, hart elaboration, and UDB on the same projection. API changes
must be exercised through the existing shared behavioral fixtures; the enabled
fetch fixture uses the RV2Wide entry point. Run `moon run rhodium:check-boundaries` after
changing imports, and follow the shared guide for behavior-specific checks.
