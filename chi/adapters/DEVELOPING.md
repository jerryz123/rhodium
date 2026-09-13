<!-- Guides changes to CHI transaction adapters. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing CHI transaction adapters

Read [README.md](README.md) and the parent
[CHI developer guide](../DEVELOPING.md) before changing this area.
The parent owns package-wide boundaries; this guide owns component extension and validation.

Preserve transaction identity and unaffected packet metadata. These adapters do not define an alternative memory protocol or own generic flow machinery.

Keep tests and authoring fixtures in [`../tests/`](../tests/), and behavioral
benches in [`tests/backend/`](../../tests/backend/DEVELOPING.md).
For source moves, update direct consumers, package documentation, and build/CI
paths together. Run `make check-boundaries` and the affected host and behavioral
checks with a fresh isolated compiled root. Directory boundaries do not add RTL
hierarchy or per-directory facade modules.

## Extension and focused validation

Bank geometry belongs to `StripedAddressLayout` in
`rhodium/std/interconnect.rhdl`. Inclusive Homes and SoCs use it directly.
`CHIAddressProjectorConfig` retains its existing constructor as a CHI-width
validation wrapper with a `layout` property; the adapter owns native-channel
forwarding and runtime base translation, not stripe arithmetic.

Fragmenter DAT/RSP translations stay private to `chi/adapters/transfer-fragmenter.rhdl` and
use immutable field replacement. DAT forwarding clears `replicate` and `num_dat`
and substitutes the child TxnID; completion forwarding restores the parent
DBID. Preserve every other field, including optional metadata, without copying
the flit schema. Run `chi-fragmenter-metadata` for complete-packet comparisons
at all DAT widths with options enabled/disabled, reverse-order input packets,
distinct child DBIDs, and stalled requests/data/responses. Its randomized
metadata checks transparency, not additional protocol-profile support. Keep
`chi-transfer-fragmenter` for the RAM-backed write/read behavior and the host
fragmenter test for service configuration and invalid transfer limits.
