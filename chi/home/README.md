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
but complete SimpleSoC D-cache return ancestry has not been validated.
[Registered branching feedback](../../rhodium/event/README.md#deliberate-limits)
has a dedicated queued-crossbar regression. The latest SimpleSoC instrumentation
attempt passes the shared subordinate and host-requester boundaries, then stops
at `soc/rv5stage/uncached`'s outgoing CHI REQ boundary. The uncached engine still
lacks a request-ownership tracing contract. No complete SoC trace was generated;
this attempt does not establish coverage of the full NoC graph. This is a tracing
coverage limit, not a change to the Home's functional request/response behavior.
