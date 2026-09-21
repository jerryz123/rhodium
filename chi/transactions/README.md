<!-- Describes the public boundary of CHI transaction mechanisms. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# CHI transaction mechanisms

Bounded transaction models and monitors, opt-in endpoint checking, retry
control, and reusable requester engines.

## Complete-line ReadOnce requester

[`read-once.rhdl`](read-once.rhdl) provides a one-outstanding RN-I requester
for retryable, cacheable 64-byte `ReadOnce` transactions. A command supplies
the physical line address, target Home, PAS, QoS, allocation hint, and opaque
context. The requester retains caller-supplied node and transaction IDs,
reassembles reordered legal DAT packets at 128-, 256-, or 512-bit CHI data
widths, reports the first non-OK response error and accumulated poison, sends
`CompAck`, and holds its result until accepted.

The allocation bit is a request to the Home; whether a Home installs a missed
line remains Home policy. The engine owns no address map or endpoint allocator.
Its RN-I endpoint must advertise `ReadOnce` and `CompAck`, accept
`RetryAck`/`PCrdGrant` and `CompData`, and reserve the supplied transaction ID
until completion transfers.

Import the defining modules directly, or use the package-wide
[`chi/main.rhdl`](../main.rhdl) facade. See the
[CHI package guide](../README.md) for public APIs, supported profiles, and limits.

Contributor ownership and validation are described in
[DEVELOPING.md](DEVELOPING.md).
