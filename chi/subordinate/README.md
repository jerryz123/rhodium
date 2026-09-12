<!-- Describes the public boundary of CHI subordinate engines and storage. -->

# CHI subordinate engines and storage

Single-beat device sequencing, transaction slots, shared memory control, SRAM backing, and DPI-backed simulation memory.

Import the defining modules directly, or use the package-wide
[`chi/main.rhdl`](../main.rhdl) facade. See the
[CHI package guide](../README.md) for public APIs, supported profiles, and limits.

Contributor ownership and validation are described in
[DEVELOPING.md](DEVELOPING.md).

## Single-beat request tracing

`CHISingleBeatSubordinate` supplies intrinsic tracing contracts from accepted
ordinary REQ traffic to its RSP and read-DAT outputs. Both outputs retain the
same request occurrence through DBID, delayed write data, and backpressure;
ownership ends on final write completion, read completion, or reset. Credit
returns do not capture or release ownership. These are request-ownership edges,
not separate write-data provenance edges.

The contracts add no checkpoints or functional buffering. Place event
annotations on surrounding Flow routes; optional compiler instrumentation
provides metadata storage and DPI emission.
