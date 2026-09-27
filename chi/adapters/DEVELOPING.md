<!-- Maps CHI adapter ownership, metadata preservation, and focused validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing CHI transaction adapters

Read the [adapter README](README.md) for the public boundary and the
[parent guide](../DEVELOPING.md) for package-wide dependency and test policy.
This guide owns adapter changes.

## Architecture and ownership

Adapters retain native CHI channels and transaction identity. They do not
define another memory protocol, own generic Flow machinery, or calculate
striped-bank geometry. `StripedAddressLayout` in
[`rhodium/std/interconnect.rhdl`](../../rhodium/std/interconnect.rhdl) owns that
geometry; inclusive Homes and SoCs use it directly.

## Implementation map

| File | Responsibility |
|---|---|
| [`transfer-fragmenter.rhdl`](transfer-fragmenter.rhdl) | Serialized child transfers and parent DataID/DBID/completion restoration |
| [`address-projector.rhdl`](address-projector.rhdl) | CHI-width validation and native-channel REQ address projection |

`CHIAddressProjectorConfig` retains its constructor and exposes a `layout`
property. The adapter translates runtime base addresses; it does not duplicate
stripe arithmetic.

## Change workflow

Preserve every unaffected packet field, including optional metadata, by
immutable replacement rather than copying the flit schema. Fragmenter DAT
forwarding clears `replicate` and `num_dat` and substitutes the child TxnID;
completion forwarding restores the parent DBID. Keep these transforms private
to the adapter. For source moves, update direct consumers, docs, and build/CI
paths together; directory boundaries add no RTL hierarchy or facade module.

## Focused validation

Tests and authoring fixtures live in [`../tests/`](../tests/); behavioral
benches live in [`../tests/circt/`](../tests/circt/). Run the host fragmenter
test for service configuration and invalid transfer limits,
`chi-fragmenter-metadata` for complete packets at all DAT widths and optional
field settings under reordering/stalls, and `chi-transfer-fragmenter` for
RAM-backed writes and reads. Metadata randomization checks transparency, not
additional protocol-profile support. Run `make check-boundaries` after source
or dependency changes; use the [parent guide](../DEVELOPING.md#focused-validation)
for broader CHI checks.
