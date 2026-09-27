<!-- Guides changes to CHI protocol contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing CHI protocol contracts

Read the [protocol README](README.md) for the public contract and the
[parent guide](../DEVELOPING.md) for package-wide boundaries. This guide owns
wire, endpoint, message, and service-contract maintenance.

## Architecture and ownership

Keep stateless packet rules below engines and monitors. This directory does
not own transaction allocation, memory storage, network topology, or a
per-directory facade. The root [implementation graph](../DEVELOPING.md#architecture-and-dependency-boundary)
owns package-wide dependency direction.

## Implementation map

| Files | Responsibility |
|---|---|
| [`params.rhdl`](params.rhdl), [`flits.rhdl`](flits.rhdl) | Physical choices and packed payloads |
| [`protocol.rhdl`](protocol.rhdl), [`coherence.rhdl`](coherence.rhdl) | Packet positions, Size/DataID rules, coherent vocabulary |
| [`link.rhdl`](link.rhdl), [`channels.rhdl`](channels.rhdl) | Credited links, endpoint capabilities, ready-valid channels |
| [`fabric.rhdl`](fabric.rhdl) | Service support, Home/subordinate maps, decode validation |
| [`messages.rhdl`](messages.rhdl) | Stateless message builders and metadata-preserving transforms |

## Change workflow

### Links and capabilities

Exact node-to-ICN peer metadata belongs to `CHINodeParams.icn_peer()` in
`chi/protocol/link.rhdl`. RAM, devices, and SoC compositions derive it there rather than
repeating capability reversal. Home placement parameters derive their
subordinate endpoint from the service; retain separate structural configuration
and runtime identity.

Credited link compatibility compares the complete immutable link parameters
structurally, then checks endpoint roles, identity, and capability containment
separately. Keep RN, RN-I, and SN link types distinct.

### Message construction

Semantic packet construction belongs in `chi/protocol/messages.rhdl`, below transaction
engines. Inclusive-Home cached data reuses the subordinate read builder and
overrides trace tag and coherent response state. Victim DAT reuses the
NonCopyBackWriteData builder and explicitly retains the Home's HomeNID, trace
tag, and QoS. Keep those policy overrides in the Home. Maintenance REQs use
immutable construction with their existing inactive-field defaults; command
PAS/SnoopMe and retry-attempt fields remain maintenance-owned.

The current RN/SN/Home RSP constructors share `chi_response` for inactive-field
initialization. Keep active routing, opcode, DBID, response-state, error, and QoS
choices in their semantic wrappers. Its zero policy does not apply to every
RSP opcode; forwarding transforms must continue preserving untouched metadata.

HN-I forwarding also lives in `chi/protocol/messages.rhdl`: immutable REQ/RSP/DAT
updates retain all untouched metadata, including optional fields. HN-I keeps
its own return routing and DBID translation policy rather than inheriting
HN-F field clearing. Historical transaction-module exports remain aliases;
production message consumers import the owner directly.

### Capabilities and service maps

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
use an optional-one-hot selector without priority.

Service opcode/encoded-Size matching belongs to `CHIRequestSupport.matches`
in `chi/protocol/fabric.rhdl`. HN-I retains address-map matching; HN-F retains opcode
translation, runtime service base, and maintenance exceptions. Do not conflate
this hardware predicate with host `.supports(opcode, bytes)` queries. HN-I
cross-field parameter checks run in `CHIHNIParams` construction; host negatives
must not require circuit elaboration.

### Message consumers and packet positions

The subordinate allocator owns occupancy, DBID association, and packet
receipt state, not response construction. Production consumers import builders
from their defining message module. Keep address maps, device side effects,
and endpoint policy with callers. Builders should construct immutable values,
not declare caller-scoped named wires. The read-completion builder's zero
literal expresses only its existing inactive-field policy; it is not a
universal default for other CHI messages.

RV5Stage and FESVR import shared `chi_rn_write_data` construction from
`chi/protocol/messages.rhdl`.
Keep their DataID, CCID, and overloaded DBID/MECID field choices in caller
wrappers, alongside data placement and address policy.

Snoop and intervention-write packet construction lives in
`chi/protocol/messages.rhdl`; its callers
choose opcode, address override, transaction identity, and early-write-ack policy.
The builders preserve the existing zero policy for inactive optional fields
and return immutable values, so repeated calls do not allocate colliding wires.

Packet position and naturally aligned, unelided transfer packet sets belong in
`protocol.rhdl`, below both engines and monitors. Use its address-aware helpers
for RAM/DPI addressing and requester, subordinate, Home, and refill logic; do
not introduce node-role-specific DataID renumbering. Each engine and monitor
keeps its own receipt state and checks duplicate/unexpected packets.

## Focused validation

Keep host tests and authoring fixtures in [`../tests/`](../tests/), and
behavioral benches in [`../tests/circt/`](../tests/circt/). Select the focused
checks for the contract changed:

| Change | Checks |
|---|---|
| Link peers, capabilities, or service parameters | Link and Home host tests; RAM/device/Home consumer simulations |
| Message builders or transforms | `chi-messages` at all DAT widths and optional-field settings; inclusive-Home, maintenance-Home, cache-maintenance, and `rv5stage-chi-requests` consumers |
| Home/subordinate maps or support matching | `chi-foundation` boundaries, sparse holes, opcodes, and Size ranges; HN-I constructor negatives |
| DataID or packet-position rules | `chi-packets` across all bus widths; `rv5stage-uncached`, `rv5stage-dcache`, `fesvr-mmio`, RAM, and fragmenter consumers |

Consumer benches must compare complete packets, not just payloads. For source
moves, update direct consumers, docs, and build/CI paths together, then run
`make check-boundaries` and affected checks through the repository wrappers.
