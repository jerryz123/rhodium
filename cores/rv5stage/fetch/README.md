<!-- Defines RV5Stage fetch-pipeline and branch-predictor public entry points. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV5Stage fetch

This package owns instruction acquisition from S0 request selection through
compressed instruction assembly at the Decode boundary. The parent
[microarchitecture guide](../README.md#microarchitecture) defines observable
stage timing, replay, fault, redirect, and reservation behavior. The sibling
[`icache/`](../icache/README.md) package owns instruction storage, refill, and
coherence.

[`RV5StageFrontend`](frontend.rhdl) is the integrated fetch pipeline.
[`protocol.rhdl`](protocol.rhdl) exports its core-facing control interface and
the final `FetchDecode` payload.
[`instruction-buffer.rhdl`](instruction-buffer.rhdl) is separately reusable by
the core and focused fixtures; `source.rhdl`, `packet.rhdl`, and `scan.rhdl` are
internal pipeline components.

Branch prediction lives under [`bpd/`](bpd/). Its
[`protocol.rhdl`](bpd/protocol.rhdl) defines prediction and training payloads,
while [`btb.rhdl`](bpd/btb.rhdl) and [`ras.rhdl`](bpd/ras.rhdl) implement the
configured BTB and return-address stack. The parent
[branch-prediction contract](../README.md#branch-prediction) defines their
observable policy and generator parameters.

See [DEVELOPING.md](DEVELOPING.md) for dependency direction, file ownership,
and focused validation.
