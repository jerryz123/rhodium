<!-- Guides contributors through RV5Stage fetch and branch-prediction source ownership. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RV5Stage fetch

Read this package's [README](README.md) and the parent
[RV5Stage guide](../DEVELOPING.md) for the complete pipeline contract. This
directory owns instruction acquisition from the S0 request through the
packet-to-Decode boundary. The sibling [`icache/`](../icache/) package owns
instruction storage, refill, and coherence rather than fetch sequencing.

## Implementation map

| Area | Ownership |
|---|---|
| [`protocol.rhdl`](protocol.rhdl) | Core/frontend control and the final fetch-to-Decode payload |
| [`frontend.rhdl`](frontend.rhdl) | S1/S2 correlation, repair, reservation, and packet queue topology |
| [`source.rhdl`](source.rhdl) | S0 PC selection, continuation, replay, and prediction lookup |
| [`packet.rhdl`](packet.rhdl), [`scan.rhdl`](scan.rhdl) | Raw packet representation, prediction-cut validation, and S2 control-flow predecode |
| [`instruction-buffer.rhdl`](instruction-buffer.rhdl) | Compressed expansion, straddling assembly, and residual-halfword state |
| [`bpd/protocol.rhdl`](bpd/protocol.rhdl) | Prediction, training, and return-stack payload contracts |
| [`bpd/btb.rhdl`](bpd/btb.rhdl) | Fully associative targets, local direction counters, S2 discovery, and replacement |
| [`bpd/ras.rhdl`](bpd/ras.rhdl) | RISC-V call/return classification and speculative/resolved stack state |

## Dependency direction

Keep `bpd/protocol.rhdl` independent of predictor implementations and fetch
sequencing. BTB and RAS implementations may depend on that protocol; the fetch
pipeline may depend on all three. Predictor modules must not import their fetch
consumers. Keep `FetchDecode` in `fetch/protocol.rhdl` rather than the scalar
pipeline's general `bundles.rhdl`, so instruction assembly does not depend back
on execution-owned payloads.

The core may consume fetch and predictor protocols and use the RAS classifier
when resolving an instruction. Tests remain under [`../tests/`](../tests/), and
backend emitters remain under [`../../../tests/backend/`](../../../tests/backend/).
Do not add root-level forwarding modules for old paths; update consumers as one
move so the directory boundary remains visible.

## Focused validation

After changing layout or imports, run `make check-boundaries` and confirm no old
paths remain. Fetch and predictor behavior is covered by the `rv5stage-btb`,
`rv5stage-ras`, `rv5stage-instruction-buffer`, `rv5stage-fetch-prediction`,
`rv5stage-fetch-throughput`, `rv5stage-return-prediction`,
`rv5stage-branch-prediction`, `rv5stage-fetch`, `rv5stage-core`, and
`event-frontend` CIRCT fixtures. Use one fresh `PLTCOMPILEDROOTS` for the focused
batch as required by the repository agent instructions.
