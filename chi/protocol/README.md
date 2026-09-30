<!-- Introduces the public CHI wire, endpoint, message, and service contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# CHI protocol contracts

Use `chi/protocol/` to describe physical CHI flits and links, ready-valid
engine channels, capabilities, services, and address maps without selecting a
transaction engine. Contributors should read [DEVELOPING.md](DEVELOPING.md).

## Get started

Import only the defining contracts needed by a component:

```rhombus
import:
  lib("chi/protocol/params.rhdl").CHIFlitParams
  lib("chi/protocol/channels.rhdl").CHIRNChannels
```

The package-wide [`chi/main.rhdl`](../main.rhdl) facade is convenient when
exploring the full API. The [CHI import guide](../README.md#package-boundary-and-import)
explains the difference.

## Public surface

| Need | Defining module |
|---|---|
| Physical parameters and packed REQ/RSP/SNP/DAT fields | [`params.rhdl`](params.rhdl), [`flits.rhdl`](flits.rhdl) |
| Packet positions, transfer Size, DataID, and coherent-state vocabulary | [`protocol.rhdl`](protocol.rhdl), [`coherence.rhdl`](coherence.rhdl) |
| Credited physical links and ready-valid engine channels | [`link.rhdl`](link.rhdl), [`channels.rhdl`](channels.rhdl) |
| Capabilities, service descriptions, and Home/subordinate maps | [`fabric.rhdl`](fabric.rhdl) |
| Immutable semantic message builders | [`messages.rhdl`](messages.rhdl) |

Physical credited links and internal ready-valid channels are distinct; there
is no implicit conversion. `CHIHomeMap` selects a Home for a requester, whereas
`CHISubordinateMap` selects a subordinate after Home processing. Message
builders retain caller-selected policy and allocate no transaction state.
See the package guide for the [wire](../README.md#wire-and-protocol-foundation),
[endpoint](../README.md#endpoint-links-and-engine-channels), and
[service-map](../README.md#services-and-system-address-maps) contracts.

## Message construction and endpoint peers

For stateless subordinate responses, import `lib("chi/protocol/messages.rhdl")` directly
or use the facade. `chi_sn_dbid_response` and `chi_sn_write_completion` correlate
responses with the request's source and TxnID and the supplied DBID;
`chi_sn_read_completion` uses its return-node/return-TxnID fields, derives byte
enables from the transfer, and accepts the packet's DataID and payload. These
builders emit successful responses with the existing inactive/default optional
fields; they do not allocate transactions, validate endpoint capabilities, or
implement retry, error, or coherence policy.
They return immutable values and can be called repeatedly in one circuit
without allocating named wires or sharing state between calls.

`chi_response` exposes the common inactive-field policy used by the current
subordinate, Home, and RV5Stage RSP builders. It takes explicit source/target
NodeIDs, TxnID, and opcode, plus required `~dbid`, `~resp`, `~error`, and `~qos`
arguments. Other fields are zero, with `tag_op` set to `Invalid`. This is not
a universal default for all RSP opcodes: callers needing Protocol Credit,
trace, or other active fields must supply their own construction or updates.
Existing semantic wrappers retain their routing and response policies.

`chi_rn_write_data` constructs the current requester `NonCopyBackWriteData`
profile from a payload, byte enables, routing IDs, and DBID. Its required
`~data_id`, `~ccid`, and `~dbid_or_mecid` arguments keep packet-position and
overloaded-field choices explicit. Inactive and optional fields are zero;
this is not a general constructor for every CHI DAT profile. Callers retain
address normalization, lane placement, masks, and cacheability policy.

`protocol/messages.rhdl` also provides Home response construction and immutable
downstream-request, snoop-write-data, and upstream-read-data transforms.
These preserve untouched packet metadata, including optional fields. Callers
supply routing identities and policy decisions; the helpers neither allocate
transactions nor choose a coherence policy.

`chi_home_snoop` constructs a non-forward snoop using an explicit address,
opcode, and TxnID. `chi_home_snoop_write_request` constructs a full-packet
backing write from intervention data, with explicit TxnID and early-write-ack
choice. Both use the existing inactive-field zero policy, including optional
metadata, rather than copying every request field.

`CHINodeParams.icn_peer()` derives the matching ICN endpoint: it appends
`-icn` to the name, retains NodeID, node kind, and outstanding limit, and swaps
emitted and supported capabilities. Use explicit `CHIICNPortParams` when the
ICN contract is intentionally different from the node's exact peer.
`CHINodeParams` and `CHISubordinateServiceParams` support immutable `with`
updates; reconstruction reruns their role and capability validation.

## Limits and navigation

Issue H opcode and flit definitions are broader than the transactions any
particular endpoint implements. Disabled optional fields are absent from a
flit's packed representation. Check the [delivered profiles](../README.md#delivered-profile-and-limits)
before advertising a capability. Use [transaction mechanisms](../transactions/README.md)
for checking and retry, or [Home](../home/README.md) and
[subordinate](../subordinate/README.md) engines for execution.
