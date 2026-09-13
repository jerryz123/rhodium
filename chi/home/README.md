<!-- Describes the public boundary of CHI Home engines. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# CHI Home engines

Non-coherent, coherent, and inclusive Home engines, shared Home policy, and snoop-target bookkeeping.

Import the defining modules directly, or use the package-wide
[`chi/main.rhdl`](../main.rhdl) facade. See the
[CHI package guide](../README.md) for public APIs, supported profiles, and limits.

Contributor ownership and validation are described in
[DEVELOPING.md](DEVELOPING.md).

## Inclusive Home event tracing

The optional event compiler carries incoming request ancestry to requester
response and data transfers using the blocking transaction's retained ownership.
The Home emits no checkpoints of its own: caller annotations connect across it
without adding intermediate Home tracks.
The same request remains the parent across LLC misses, snoops, and writebacks;
transaction IDs may be reused without confusing occurrences. Request ownership
ends at actual transaction completion, including CompAck when required, or reset.
Ordinary elaboration adds no event instrumentation or functional buffering.

These edges describe request ownership, not backing-memory data provenance.
Subordinate traffic and incoming snoop/data contributions are not separately
represented in this graph yet.

Current integration limit: the standalone instrumented Home regression passes,
but complete SimpleSoC D-cache return ancestry has not been validated.
[Registered branching feedback](../../rhodium/event/README.md#deliberate-limits)
has a dedicated queued-crossbar regression. The SimpleSoC emitter enables
[partial tracing](../../rhodium/event/README.md), so missing contracts such as
the uncached engine's ownership become explicit ancestry gaps instead of
blocking all instrumentation. IO-MSHR and uncached ownership remain unannotated.
Checkpoints can supply parents to downstream annotations without a leaf
declaration. SimpleSoC partial instrumentation, CIRCT IR verification, and
SystemVerilog lowering pass.
Full NoC graph coverage still requires runtime integration validation;
partial tracing does not change the Home's functional request/response behavior.
