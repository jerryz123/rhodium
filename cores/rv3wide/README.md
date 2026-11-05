<!-- Documents RV3Wide's three-lane specialization and shared execution services on a 4R2W pipeline. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV3Wide

RV3Wide is an experimental named entry point to the
[shared superscalar pipeline](../superscalar/README.md). It can issue and retire
three instructions per cycle; all three lanes can use the existing shared
memory, FP, vector, and integer arithmetic services. Contributor
ownership and validation are in [DEVELOPING.md](DEVELOPING.md).

## Get started

```rhdl
import:
  lib("cores/rv3wide/rv3wide.rhdl").RV3Wide
  lib("cores/rv3wide/profile.rhm").RV3WideConfig
  lib("cores/cache/config.rhm").CacheConfig

inst core(RV3Wide(CacheConfig(64, 2), ~chi: config))
```

The default configuration selects scalar RV64IMACB with the shared system
features and Sv39, without FP or vectors. Configuration arguments and
architectural capabilities follow the shared implementation.

## Public contract and limits

`rv3wide.rhdl`, `hart.rhdl`, `profile.rhm`, and `udb.rhm`
export `RV3Wide`, `RV3WideHart`, `RV3WideConfig`, and
`rv3wide_udb_configuration`. `RV3WideConfig` is a distinct immutable class,
not an alias of RV2WideConfig or SuperscalarPipelineConfig. Its sole field is
`pipeline :: SuperscalarPipelineConfig`, defaulting to `SuperscalarPipelineConfig()`.
The shared type owns parameters, defaults, and validation; the wrapper exposes
only `isa_profile`, `csr_config`, and `hart_description` as metadata conveniences.
For example, `RV3WideConfig(SuperscalarPipelineConfig(~btb_entries: 16))`
selects a smaller BTB. Import the pipeline type from `cores/superscalar/profile.rhm`.
`RV3WideHart` independently
owns reset/start and CHI wiring. `RV3Wide` is a host factory that projects
configuration without adding pipeline hierarchy. For the execution slice alone,
use `SuperscalarCore(profile.pipeline, ~issue: RV3WideIssue(profile.pipeline.xlen))` from
`cores/superscalar/core.rhdl` directly. UDB uses the
same contained pipeline config; there is still only one execution implementation.

Fetch remains eight bytes per cycle, issue/retirement support three lanes, and the
GPR file remains 4R2W. Pipeline payloads come from `cores/superscalar/bundles.rhdl`.
There is no backend lane-count parameter. `issue.rhdl` exports
`RV3WideIssue(xlen)`, a three-candidate scheduling policy with fixed read-path
borrowing and two-write-port contention. The factory supplies it to the shared
core. Lane 3 borrows lane 2 rs2's read/bypass path for rs1 and lane 1 rs2's path
for rs2, only when those donor operands are unused or x0. Issue selects
an older move's captured read path when a same-group copy
dependency permits sharing; this works even when the usual donor is occupied.
Issue assigns the write port from the two existing ports. Three distinct direct GPR writes cannot
coissue; same-destination overwrites can share capacity without merging retirements.

All three lanes can execute branches, loads/stores, FP, vector, and multiply/divide
operations. This does not add execution-service bandwidth: same-group conflicts,
operand borrowing, destination hazards, and write-port availability still limit
admission. Serializing system commands still issue alone. Fixed arithmetic can
launch speculatively in EX; WB authorizes architectural effects after every older
lane succeeds. Fault/replay in any lane preserves its successful older prefix
and suppresses younger effects. Vector memory retains its own owner when needed;
only its unretired younger suffix is replayed.

RV3Wide has the same supported shape/ISA selections and CI enrollment as RV2Wide:
Mini enrolls `rv32int`, `rv32max`, `rv64imacb`, `rv64imafdcb`, and `rva23`;
Simple additionally enrolls `rv64max`; Tiled4Core and Tiled8Core enroll `rva23`.
All four RV64 presets remain supported manual selections on both tiled shapes,
and Mini RV64Max remains a manual selection. Use complete keys such as
`mini-rv3wide-rv64imacb` or `simple-rv3wide-rva23`.

Software coverage follows the shared shape/ISA policy, including Mini/Simple
Sail cosim, Simple ISA/ACT suites, and tiled multihart workloads. The existing
shared disabled-prediction fetch fixture also exercises this entry point
against the same behavioral oracle as RV2Wide. See the
[shared contract](../superscalar/README.md) for all other interfaces and limits.
