<!-- Defines the public configuration boundary for the future Spike-backed core. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Spike-backed core configuration

[`profile.rhm`](profile.rhm) defines `SpikeConfig`, the implementation-owned
configuration shell for a future simulator-backed Spike core. Its
`hart_description` projection uses the same implementation-neutral
[`RiscvHartDescription`](../../riscv/isa/hart.rhm) consumed by SoC and
device-tree code.

This package does not yet provide the Spike execution adapter, DPI bridge, or
CHI transaction implementation.
