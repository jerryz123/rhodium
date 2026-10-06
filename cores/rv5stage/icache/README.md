<!-- Describes RV5Stage fetch metadata and the shared instruction-cache attachment. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV5Stage instruction-memory contracts

This directory owns RV5Stage's fetch attempt, translation, and fault metadata
in [`protocol.rhdl`](protocol.rhdl). The physical cache lives in
[`cores/cache/l1i/`](../../cache/l1i/README.md). Contributors should read
[DEVELOPING.md](DEVELOPING.md).

RV5Stage directly instantiates `L1ICache` with `~fetch_bits: 32` for both XLENs.
The instruction-memory router converts shared block/error/replay outcomes into
`RV5StageFetchResult`; the MMU supplies page faults and guest provenance.
Frontend instruction assembly, replay, and fault ownership remain unchanged.

The [core memory hierarchy](../README.md#memory-hierarchy) owns PMA routing and
`FENCE.I` ordering. No RV5Stage cache wrapper or compatibility alias is needed.
