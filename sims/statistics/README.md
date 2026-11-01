<!-- Describes the native sampled-count collector and its simulator lifecycle integration. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Native statistics collector

`Collector` accumulates the sites emitted by the optional
[statistics RTL pass](../../rhodium/statistics/README.md). Simulator users enable
[`STATS=1`](../README.md#rtl-statistics); contributors follow
[the statistics guide](../../rhodium/statistics/DEVELOPING.md) and
[simulation development](../DEVELOPING.md).

Construct a collector with the generated descriptor array. Bracket each rising
edge with `begin(reset)` and `end()`, and call `count(id)` for each enabled site.
`freeze()` after a settled exit edge is idempotent; subsequent samples may drain
normally but do not change counts. `json()` requires frozen, settled state.

Counts use checked unsigned 64-bit arithmetic. Duplicate site samples in one
edge, invalid IDs, invalid lifecycle calls, and overflow throw errors. The DPI
binding in `simulation_runtime.cc` catches and latches those errors, so they fail
the run instead of crossing the foreign ABI. Reset samples are suppressed even
though generated calls already gate their reset. Reset does not erase earlier
samples; a new run constructs a new collector. No external JSON library or
event-tracing runtime is required.
