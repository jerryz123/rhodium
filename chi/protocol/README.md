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

## Limits and next steps

Issue H opcode and flit definitions are broader than the transactions any
particular endpoint implements. Disabled optional fields are absent from a
flit's packed representation. Check the [delivered profiles](../README.md#delivered-profile-and-limits)
before advertising a capability. Use [transaction mechanisms](../transactions/README.md)
for checking and retry, or [Home](../home/README.md) and
[subordinate](../subordinate/README.md) engines for execution.
