<!-- Describes the public boundary of CHI Home engines. -->

# CHI Home engines

Non-coherent, coherent, and inclusive Home engines, shared Home policy, and snoop-target bookkeeping.

Import the defining modules directly, or use the package-wide
[`chi/main.rhdl`](../main.rhdl) facade. See the
[CHI package guide](../README.md) for public APIs, supported profiles, and limits.

Contributor ownership and validation are described in
[DEVELOPING.md](DEVELOPING.md).

## Inclusive Home event tracing

The optional event compiler connects `home.request` to every `home.response`
and `home.data` transfer using the blocking transaction's retained ownership.
The same request remains the parent across LLC misses, snoops, and writebacks;
transaction IDs may be reused without confusing occurrences. Request ownership
ends at actual transaction completion, including CompAck when required, or reset.
Ordinary elaboration adds no event instrumentation or functional buffering.

These edges describe request ownership, not backing-memory data provenance.
Subordinate traffic and incoming snoop/data contributions are not separately
represented in this graph yet.

Current integration limit: the standalone instrumented Home regression passes,
but SimpleSoC event instrumentation encounters a structural response-router
cycle. The event compiler does not yet lower stateful Flow feedback between
checkpoints, so the enabled D-cache return ancestry does not currently produce
a complete SoC trace. This is a tracing limitation, not a change to the Home's
functional request/response behavior.
