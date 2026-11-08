<!-- Introduces superscalar memory timing, translation specialization, and physical cache attachment. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Superscalar memory integration

This directory connects the superscalar pipeline to translation and physical
memory services. The integrated entry point is
[`Superscalar`](../superscalar.rhdl); see the [package contract](../README.md)
and [contributor guide](../DEVELOPING.md#implementation-map).

`ports.rhdl` arbitrates scalar/vector ownership. `adapter.rhdl` owns pipeline
timing, qualification, and split accesses; `translation.rhdl` defines its
translation query/result contract. The identity and Sv39 specializations pair
their implementation with the capabilities in `translation-specialization.rhm`.
`transport.rhdl` routes accepted physical requests and responses.
`attachment.rhdl` implements `SuperscalarL1D`, including permissions, ordering,
and cached/uncached routing around the shared
[`L1DCache`](../../cache/l1d/README.md).
