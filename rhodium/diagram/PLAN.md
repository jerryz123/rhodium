<!-- Records the incremental plan for Rhodium logical circuit visualization. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Logical visualization plan

## Completed first slice

- Define an immutable diagram model independent of Graphviz or a browser UI.
- Extract verified module hierarchy, circuit boundaries, instances, registers,
  compound interfaces, and adjacent typed channels.
- Preserve flow intent with nonsemantic frontend/library provenance rather than
  adding flow operations to the core hardware IR.
- Recognize map, filter, pipe, queue, gate, conversion, fork, demultiplex, zip,
  round-robin arbitration, grant-crossbar, and credited-transport stages.
- Emit deterministic JSON and a compact DOT diagnostic view.
- Classify boundaries, combinational blocks, and blocks that recursively hold
  synchronous state without conflating behavior with logical block kind.
- Preserve each channel's nominal protocol family and style the standard
  ready-valid protocols independently of their payload type.
- Render modules as clusters with ranked boundary anchors and route every DOT
  edge through named ports on table-shaped internal blocks.
- Omit implicit `sync_circuit` clock/reset ports and their instance paths while
  preserving explicitly authored control ports on ordinary circuits.

## Next increments

1. Label the remaining standard N-to-M transforms, including arbitration,
   demultiplexing, zipping, broadcasting, and crossbars.
2. Add explicit user-authored grouping and collapse annotations that lower to
   the same generic metadata mechanism.
3. Build an interactive JSON consumer with hierarchy expansion, protocol-aware
   port rendering, search, and selective primitive expansion.
4. Add optional overlays for widths, latency, clock/reset domains, and
   simulation activity without changing the base diagram model.
5. Define a separate composition point for logical diagrams and RFPL physical
   views; neither view should own or mutate the other.

Each increment should be tested against the extracted model. Pixel snapshots
should be reserved for a later renderer whose visual layout is itself a public
contract.
