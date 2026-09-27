<!-- Introduces the public CHI connection compiler and NoC attachment surface. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# CHI network integration

Use `chi/noc/` to compile CHI endpoint relationships into validated NoC routes
and attach typed REQ/RSP/DAT/SNP channels to a physical router family. It does
not choose the topology or routing policy. Contributors should read
[DEVELOPING.md](DEVELOPING.md).

## Get started

Describe RN, HN, and SN sites and their logical connections with
[`noc-authoring.rhm`](noc-authoring.rhm), then pass the compiled plans to the
typed attachment helpers in [`noc-adapter.rhdl`](noc-adapter.rhdl). The
[package NoC guide](../README.md#noc-compilation-and-transport) shows the
`CHINoCPorts` attachment pattern; [`chi/main.rhdl`](../main.rhdl) remains the
convenience facade.

## Public contract

`CHIRNIConnection`, `CHIRNFConnection`, and `CHISNConnection` compile each
channel family independently and retain route-key and terminal provenance.
Fixed and family attachments share the same typed plane wiring. REQ, RSP, and
DAT use their target NodeID; SNP uses its separate dispatch target and carries
no target field in the flit. Ejection supplies one queue before the adapter;
injection adds no queue.

[`noc-router.rhdl`](noc-router.rhdl) composes three planes for non-coherent
traffic or four when SNP is required. HN requester and subordinate sides may
be attached together or on separate fabrics. Generic router slots, topology,
and routing validation belong to the [NoC package](../../noc/README.md), not
this CHI layer.

## Limits and next steps

A compiled plan is required before RTL attachment; the CHI adapter does not
perform runtime pathfinding or insert a credited-link bridge. See the
[CHI delivered profile](../README.md#physical-and-link-profile) and the
[parent guide](../README.md#build-an-end-to-end-path) for system composition.
