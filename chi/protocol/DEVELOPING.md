<!-- Guides changes to CHI protocol contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing CHI protocol contracts

Read [README.md](README.md) and the parent
[CHI developer guide](../DEVELOPING.md) before changing this area.
The parent owns package-wide boundaries; this guide owns component extension and validation.

Keep stateless packet rules below engines and monitors. This directory does not own transaction allocation, memory storage, or network topology.

Keep tests and authoring fixtures in [`../tests/`](../tests/), and behavioral
benches in [`tests/backend/`](../../tests/backend/DEVELOPING.md).
For source moves, update direct consumers, package documentation, and build/CI
paths together. Run `make check-boundaries` and the affected host and behavioral
checks with a fresh isolated compiled root. Directory boundaries do not add RTL
hierarchy or per-directory facade modules.

## Extension and focused validation

Exact node-to-ICN peer metadata belongs to `CHINodeParams.icn_peer()` in
`chi/protocol/link.rhdl`. RAM, devices, and SoC compositions derive it there rather than
repeating capability reversal. Home placement parameters derive their
subordinate endpoint from the service; retain separate structural configuration
and runtime identity. Host link and Home tests cover derivation and service
compatibility, while RAM/device/Home simulations cover connected consumers.

Credited link compatibility compares the complete immutable link parameters
structurally, then checks endpoint roles, identity, and capability containment
separately. Keep RN, RN-I, and SN link types distinct. The link host tests
construct independent equal values and vary every credit field and flit width.

Semantic packet construction belongs in `chi/protocol/messages.rhdl`, below transaction
engines. Inclusive-Home cached data reuses the subordinate read builder and
overrides trace tag and coherent response state. Victim DAT reuses the
NonCopyBackWriteData builder and explicitly retains the Home's HomeNID, trace
tag, and QoS. Keep those policy overrides in the Home. Maintenance REQs use
immutable construction with their existing inactive-field defaults; command
PAS/SnoopMe and retry-attempt fields remain maintenance-owned. Run the message,
inclusive-Home, both maintenance-Home, and cache-maintenance fixtures for these
builders; consumer benches compare full packets, not only payloads.

The current RN/SN/Home RSP constructors share `chi_response` for inactive-field
initialization. Keep active routing, opcode, DBID, response-state, error, and QoS
choices in their semantic wrappers. Its zero policy does not apply to every
RSP opcode; forwarding transforms must continue preserving untouched metadata.
Run `chi-messages` and `rv5stage-chi-requests` for complete packet comparisons,
alongside the Home and cache consumer fixtures.

HN-I forwarding also lives in `chi/protocol/messages.rhdl`: immutable REQ/RSP/DAT
updates retain all untouched metadata, including optional fields. HN-I keeps
its own return routing and DBID translation policy rather than inheriting
HN-F field clearing. Historical transaction-module exports remain aliases;
production message consumers import the owner directly.

Bounded capability predicates belong beside `CHIChannelCapabilities` in
`chi/protocol/link.rhdl`; requester and subordinate non-coherent checks use the
same channel sets with reversed directions. These describe supported subsets,
not selectable monitor profiles. Coverage attachment and checker state remain
in `transactions/`. Home configuration must not import a checker just to
validate capabilities.

Home and subordinate maps share a private decode helper in
`chi/protocol/fabric.rhdl`, but retain distinct service validation and nominal
result types. Preserve zero NodeID on misses, including a valid hit on NodeID
zero; construction rejects overlapping regions, allowing the shared decode to
use an optional-one-hot selector without priority. `chi-foundation`
sweeps both hardware maps over region boundaries and sparse holes.

Service opcode/encoded-Size matching belongs to `CHIRequestSupport.matches`
in `chi/protocol/fabric.rhdl`. HN-I retains address-map matching; HN-F retains opcode
translation, runtime service base, and maintenance exceptions. Do not conflate
this hardware predicate with host `.supports(opcode, bytes)` queries. The
foundation fixture sweeps all opcodes and encoded sizes across representative
single-size and bounded ranges. HN-I cross-field parameter checks run in
`CHIHNIParams` construction; host negatives must not require circuit elaboration.

The subordinate allocator owns occupancy, DBID association, and packet
receipt state, not response construction. Production consumers import the builders from their defining message module. Keep address maps,
device side effects, and endpoint policy with callers. The `chi-messages`
simulation checks routing, byte masks, payloads, and default fields at all DAT
widths with optional fields enabled and disabled, including repeated calls in
one circuit with distinct inputs. Builders should construct immutable values,
not declare caller-scoped named wires. The read-completion builder's zero
literal expresses only its existing inactive-field policy; it is not a
universal default for other CHI messages.

RV5Stage and FESVR import shared `chi_rn_write_data` construction from
`chi/protocol/messages.rhdl`.
Keep their DataID, CCID, and overloaded DBID/MECID field choices in caller
wrappers, alongside data placement and address policy. The message fixture
checks every output bit, including inactive optional fields, across DAT widths;
`chi-packets`, `rv5stage-uncached`, `rv5stage-dcache`, and `fesvr-mmio` cover the
packet rules and engine consumers.

The message constructor fixture compares complete transformed packets at every DAT
width, with optional REQ/DAT metadata enabled and disabled. Run it alongside
both Home and maintenance fixtures when changing these transforms. Snoop and
intervention-write packet construction lives in `chi/protocol/messages.rhdl`; its callers
choose opcode, address override, transaction identity, and early-write-ack policy.
The builders preserve the existing zero policy for inactive optional fields
and return immutable values, so repeated calls do not allocate colliding wires.

Packet position and naturally aligned, unelided transfer packet sets belong in
`chi/protocol/protocol.rhdl`, below both engines and monitors. Use its address-aware helpers
for RAM/DPI addressing and requester, subordinate, Home, and refill logic; do
not introduce node-role-specific DataID renumbering. Each engine and monitor
keeps its own receipt state and checks duplicate/unexpected packets. The
`chi-packets` backend fixture compares all three bus widths with independent
byte-enumeration expectations and runs RV5Stage write constructors through DAT
monitoring; RAM and fragmenter fixtures cover storage and multibeat retirement.
