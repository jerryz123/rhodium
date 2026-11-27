<!-- Describes the row-banked physical data cache selected by RV3Wide. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Row-banked L1D

RV3Wide selects `BankedL1DCache` from `cores/common/cache/banked-l1d/cache.rhdl`.
It exposes the same [lookup, authorization, and ordered response contract](../README.md)
as the simple cache. There is still only one external demand port; internal
banking does not let the core issue two scalar loads at once.

Four single-ported data banks per way are interleaved by **row**, not by
line or way. On RV64, consecutive 16-byte rows of one line occupy banks
0, 1, 2, and 3. Tags and permissions are banked by set instead, since a
cache line has just one tag. Each lookup needs all its required banks in
the same cycle.

The fresh-demand path can read alongside the older service/replay path when
their banks and ordering constraints permit. A speculative conflict replays;
an authorized request stays queued. Dirty victim reads and refill writes
use the row banks directly. A held outgoing CHI data beat does not reserve
the SRAM bank, but its victim stays protected until copyback finishes.
Two misses to distinct sets can request dirty copybacks independently. The first
granted victim can drain even if the older miss is still waiting for its grant.
The two engines share one victim reader and one outgoing CHI data port;
refills can finish out of order, but authorized responses stay ordered.

Same-line loads can share an acquisition. A younger store may acquire a
distinct line early, but its byte mask, data, and acknowledgement stay in
request order. A probe that revokes its ownership forces a fresh lookup.
These are properties of the banked cache, not configuration options on the
[simple L1D](../l1d/README.md).

See the [contributor guide](DEVELOPING.md) for miss ownership and focused fixtures.
