<!-- Describes the public boundary of CHI Home engines. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# CHI Home engines

Non-coherent, coherent, and inclusive Home engines, shared Home policy, and snoop-target bookkeeping.

Import the defining modules directly, or use the package-wide
[`chi/main.rhdl`](../main.rhdl) facade. See the
[CHI package guide](../README.md) for public APIs, supported profiles, and limits.

Contributor ownership and validation are described in
[DEVELOPING.md](DEVELOPING.md).

`CHIInclusiveHNFPhase` names internal implementation states, not protocol
states defined by CHI. It remains exported only for source compatibility.

`CHIInclusiveHNFConfig` defaults to four outstanding completion-
acknowledgement entries. Set `~comp_ack_entries` to size this bounded DBID table
for the expected acknowledgement latency and requester concurrency.
Set `~victim_writeback_entries` to the positive number of dirty replacement
lines that may progress independently of their refills; it defaults to one.
Demand slots use the low subordinate transaction IDs and writeback entries use
the following IDs. CHI limits one requester to 1024 outstanding transactions,
so the configuration enforces that combined limit even though TxnID is 12 bits.
`CHIHNFConfig(~transaction_slots: ...)` separately selects one through 64 live
coherent transactions. `CHIInclusiveHNF` uses those slots as Home TxnIDs/DBIDs,
permits distinct sets to overlap, and serializes requests that address an owned
set. An LLC hit may return while a distinct-set miss waits for subordinate
memory; non-final fill data can also enter the miss slot while the hit response
is stalled. Once a buffered writeback owns a resolved dirty victim, its parent
slot may issue the replacement refill without waiting for the backing write
completion. The new line is not installed or returned until that completion
succeeds. A failed writeback drains an issued refill and restores the complete
post-snoop victim in its reserved way. Resident snoops issue on consecutive
accepted cycles with distinct transaction IDs and may complete out of order;
the owning Home transaction advances only after every targeted RN-F completes.
Final fill installation retains exclusive use of the shared LLC array port.
The transaction-slot parameter defaults to one for compatibility; the complete
single-core and tiled SoCs select two.

## Inclusive Home event tracing

For a one-slot configuration, the optional event compiler carries incoming
request ancestry to requester response and data transfers using the retained
transaction ownership.
The Home emits no checkpoints of its own: caller annotations connect across it
without adding intermediate Home tracks.
The same request remains the parent across LLC misses, snoops, and writebacks;
transaction IDs may be reused without confusing occurrences. Request ownership
ends at its final response or DAT transfer, or reset. For reads that require
`CompAck`, `CHIInclusiveHNF` assigns a DBID from its configured acknowledgement
table and releases the LLC datapath after final DAT. A later `CompAck` retires
only that table entry, so unrelated lookup, snoop, refill, and response work can
continue while acknowledgements are outstanding.
Ordinary elaboration adds no event instrumentation or functional buffering.
Multi-slot retained-event ancestry is not yet represented by the event model;
the focused instrumented Home fixture therefore uses the one-slot configuration.

These edges describe request ownership, not backing-memory data provenance.
Subordinate traffic and incoming snoop/data contributions are not separately
represented in this graph yet.

Current integration limit: the standalone instrumented Home regression passes,
but complete SingleCoreRV5StageSoC D-cache return ancestry has not been validated.
[Registered branching feedback](../../rhodium/event/README.md#deliberate-limits)
has a dedicated queued-crossbar regression. The SingleCoreRV5StageSoC emitter enables
[partial tracing](../../rhodium/event/README.md), so missing contracts such as
the uncached engine's ownership become explicit ancestry gaps instead of
blocking all instrumentation. IO-MSHR and uncached ownership remain unannotated.
Checkpoints can supply parents to downstream annotations without a leaf
declaration. SingleCoreRV5StageSoC partial instrumentation, CIRCT IR verification, and
SystemVerilog lowering pass.
Full NoC graph coverage still requires runtime integration validation;
partial tracing does not change the Home's functional request/response behavior.
