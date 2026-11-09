<!-- Documents RV2Wide's named entry points into the shared superscalar pipeline. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV2Wide

RV2Wide is the two-wide configuration of the shared
[superscalar pipeline](../superscalar/README.md). Its implementation,
architectural capabilities, pipeline interfaces, and behavioral tests live
there. Read [DEVELOPING.md](DEVELOPING.md) for contributor routing.

## Get started

```rhdl
import:
  lib("cores/rv2wide/rv2wide.rhdl").RV2Wide
  lib("cores/rv2wide/profile.rhm").RV2WideConfig
  lib("cores/common/cache/config.rhm").CacheConfig

inst core(RV2Wide(CacheConfig(64, 2), ~chi: config))
```

Existing SoC selections remain unchanged:

```sh
SOC=mini-rv2wide-rv64imacb moon run rhodium:local-simulator
SOC=simple-rv2wide-rv64imacb moon run rhodium:sim-isa-smoke-test
```

## Public contract

`rv2wide.rhdl`, `hart.rhdl`, `profile.rhm`, and `udb.rhm`
export `RV2Wide`, `RV2WideHart`, `RV2WideConfig`, and
`rv2wide_udb_configuration`. `RV2WideConfig` contains one immutable
`pipeline :: SuperscalarPipelineConfig`, defaulting to `SuperscalarPipelineConfig()`.
The shared type owns all pipeline parameters, defaults, validation, and
architectural descriptions; the named wrapper does not copy its fields.
For example, `RV2WideConfig(SuperscalarPipelineConfig(~btb_entries: 16))`
selects a smaller BTB. Import the pipeline type from `cores/superscalar/profile.rhm`.
The wrapper exposes `isa_profile`, `csr_config`, and `hart_description` for
SoC integration. Hardware consumers use `profile.pipeline` directly.
`RV2Wide` passes that contained value without adding RTL hierarchy.
The factory supplies `RV2WideIssue(xlen)` from `issue.rhdl`, which owns the
two-candidate admission decision. For the execution slice alone, use
`SuperscalarCore(profile.pipeline, ~issue: RV2WideIssue(profile.pipeline.xlen))`
from `cores/superscalar/core.rhdl` directly.
`RV2WideHart` owns reset/start and CHI wiring around the shared fetching top.
UDB consumes the same contained config. Two-lane interfaces, timing, ISA
selections, and reset/retirement semantics are unchanged.

Import pipeline payloads and implementation components directly from
`cores/superscalar/`; they use the `Superscalar` prefix. There is no issue-width
parameter. Both named entry points currently use the same fixed two-lane
pipeline, eight-byte fetch path, and 4R2W GPR storage.

See the [shared public contract](../superscalar/README.md) for supported
profiles, memory ordering, prediction, FP/vector integration, and limitations.
