<!-- Introduces the superscalar block-fetch, prediction-refinement, and instruction-assembly implementation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Superscalar fetch

This directory implements the eight-byte fetch frontend used by RV2Wide and
RV3Wide. The integrated entry point is
[`Superscalar`](../superscalar.rhdl); see the
[package contract](../README.md#branch-prediction) for prediction behavior and
[contributor guide](../DEVELOPING.md#predictor-ownership-and-recovery) for ownership.

`frontend.rhdl` composes block fetching and correction; `direction.rhdl` refines
predictions using actual instruction boundaries; `predecode.rhdl` shares
instruction classification with `instruction-assembler.rhdl`.
`instruction-memory.rhdl` adapts cached and uncached fetch outcomes.
Shared prediction tables remain in [`cores/common/bpred`](../../common/bpred/README.md), and
the physical instruction cache remains in [`cores/common/cache`](../../common/cache/README.md).
