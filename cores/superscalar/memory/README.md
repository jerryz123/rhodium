<!-- Introduces superscalar memory timing, translation specialization, and physical cache attachment. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Superscalar memory integration

This directory connects the superscalar pipeline to translation and physical
memory services. The integrated entry point is
[`Superscalar`](../superscalar.rhdl); see the [package contract](../README.md)
and [contributor guide](../DEVELOPING.md#implementation-map).

`ports.rhdl` uses the [shared scalar/vector arbiter](../../common/memory-arbiter.rhdl)
for lookups and round-robin transactions, and retains split-access ownership. `adapter.rhdl` owns pipeline
timing, qualification, and split accesses; `translation.rhdl` defines its
translation query/result contract. Data resolution supplies the physical address,
memory type, fault class, cause, and guest details. The adapter supplies the
original VA when it builds a WB fault and forwards valid physical lookup faults at MEM.
The identity and Sv39 specializations pair
their implementation with the capabilities in `translation-specialization.rhm`.
`transport.rhdl` routes accepted physical requests and responses.
`attachment.rhdl` implements `SuperscalarL1D`. It qualifies physical requests,
adapts wide cache beats and IO byte lanes, and connects the shared
[`L1DCache`](../../common/cache/l1d/README.md) to the
[ordered memory attachment](../../common/cache/ordered-service.rhdl).
