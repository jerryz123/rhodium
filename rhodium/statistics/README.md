<!-- Describes optional per-instance RTL cycle counters and compile-target instrumentation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RTL statistics

Declare passive counters beside RTL, then select collection at compilation.
The only operation is `stat_count`. Contributors should read
[DEVELOPING.md](DEVELOPING.md).

```rhombus
import:
  lib("rhodium/frontend/layers/statistics.rhm").stat_count

sync_circuit Example():
  input miss: Bool
  stat_count("cache_misses", miss)
```

The independently imported layer works with the ordinary Rhodium profile.
Inside a synchronous context, `stat_count(name, condition)` observes a hardware
`Bool` before each rising edge and counts one when true and reset is inactive.
It counts samples, not transitions. A condition held true for three cycles
counts three. For transfers, supply `endpoint.fire()`.
Names are ASCII identifiers, unique within one module. Hardware `when`/`switch`
blocks are not supported; include their guard in the condition instead.

The declaration adds only remappable metadata: no counter registers, output
ports, or DPI calls. Enable it with a compile target:

```rhombus
import:
  lib("rhodium/compile/pipeline.rhm").rtl_pipeline_target
  lib("rhodium/statistics/pass.rhm").statistics_pass

def target = rtl_pipeline_target(backend_target, [statistics_pass()])
```

The pass preserves functional ports and instance paths, and emits ordinary DPI
calls plus `statistics.json` and `statistics.h`. Each occurrence has a separate
numeric ID, described by its path relative to the top and local name. A repeated
child definition does not merge counters. The header supplies JSON key prefixes
in ID order for the native collector, using `rtl/<instance path>/<counter>`.
Each path component escapes `~` as `~0` and `/` as `~1`. The collector reports a
flat name-to-value map; neither generated artifact belongs in source control.

The initial pass requires all observations to resolve through transparent aliases
and instance bindings to the selected top inputs (`clock` and `reset` by default;
override using `~clock` and `~reset`). Derived clocks and distinct reset domains
are rejected. This is deliberately a single-domain facility.

The [simulation harness](../../sims/README.md#rtl-statistics) enables the pass with
`STATS=1`, collects checked 64-bit counts, and reports every declaration including
zero counts. Collection ends at the first HTIF exit, inclusive of that cycle;
post-exit drain does not count. Failed runs do not publish successful execution
statistics. No synthesis counters, sums, maxima, or histograms are provided.
