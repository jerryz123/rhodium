<!-- Introduces processor implementations, named variants, and reusable common components. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Processor cores

This package contains processor implementations and their named configurations.
Start with [RV5Stage](rv5stage/README.md), [RV2Wide](rv2wide/README.md),
[RV3Wide](rv3wide/README.md), or the [Spike adapter](spike/README.md).
Contributors should read [DEVELOPING.md](DEVELOPING.md).

Reusable execution blocks, fetch protocols, predictors, caches, translation,
CSR state, floating point, vector engines, and integration contracts live in
[`common/`](common/README.md). Import them through `cores/common/...`.
The [superscalar family](superscalar/README.md) is one pipeline implementation
selected by RV2Wide and RV3Wide, with its own frontend and stage ownership.

```text
cores/
  common/         reusable cross-pipeline components and their tests
  rv5stage/       five-stage pipeline implementation
  superscalar/    pipeline implementation shared by RV2Wide and RV3Wide
  rv2wide/        named configuration, issue policy, and hart binding
  rv3wide/        named configuration, issue policy, and hart binding
  spike/          simulation-backed implementation
```

## Named cores

A pipeline implementation owns decode, datapath, architectural state, stage
timing, recovery, and retirement. Named variants select an implementation,
configuration, issue policy, and hart binding. They use common components
through those components' documented controls and timing contracts.

RV5Stage is the full RTL named core. Its default profile is integer-only;
supported optional profiles are RV32F on `XLen.X32` and RV64D on `XLen.X64`,
with the D profile also implementing F. RV32D and an RV64F-only specialization
are rejected. Compressed instructions use composable Zc selections with Zca
and the FP-profile-dependent C composition as presets and Zcb as an orthogonal
extension. See [`rv5stage/README.md`](rv5stage/README.md) for the owned
instruction families, pipeline and completion contracts, FP state and
execution, memory hierarchy, CHI boundary, generator parameters, ports, tests,
and deliberate limits.

[`RV2Wide`](rv2wide/README.md) is an in-progress dual-issue in-order core. Its
current RR-through-WB slice reuses the shared ALU, branch resolver, load/store
datapaths, and scoreboard. It supports dual retirement, speculative hit lookup,
WB-authorized memory, and separately owned deferred completions. Its cached
composition fetches 64-bit instruction blocks through the shared L1I and uses
the [shared physical L1D](common/cache/README.md), both also used by RV5Stage. It has
shared CSR/trap and translation machinery, precise faults, and Mini/Simple SoC
selection. See its [README](rv2wide/README.md) for the current ISA, memory,
retirement, cosim, and tracing contracts.

RV2Wide and the experimental [RV3Wide](rv3wide/README.md) have independent
configuration classes and hart wrappers selecting one [superscalar implementation](superscalar/README.md).
RV3Wide has three execution lanes and 4R3W GPR storage; RV2Wide has two lanes
and 4R2W storage. Its SoC selections and CI workloads mirror RV2Wide. Both share decode, frontend, MMU/cache
integration and the existing behavioral fixtures; named configs project into
the shared implementation configuration.

[`spike/spike.rhdl`](spike/spike.rhdl) is the standalone simulator-backed named
core. It projects the shared architectural hart description, runs Spike through
a typed DPI transaction boundary, keeps PMA classification in RTL, and exposes
independent instruction, coherent-data, and uncached CHI ports.
The [single-core SoC](../socs/README.md) composes that boundary with the shared
coherent platform through its explicit core selection. See
[`spike/README.md`](spike/README.md) for the adapter, private-cache, and
simulation contracts.
