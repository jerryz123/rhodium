<!-- Guides changes to RV5Stage fetch metadata and shared-L1I integration. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RV5Stage instruction-memory contracts

Read [README.md](README.md) for the attachment and
the [shared L1I guide](../../cache/l1i/DEVELOPING.md) for physical cache changes.

`protocol.rhdl` owns RV5Stage's S0/S2 fetch interface, S1 translated lookup,
32-bit instruction response, page faults, and optional guest provenance.
`../instruction-memory-router.rhdl` maps the physical cache result into this
contract and retains ordered uncached fetches. `../rv5stage.rhdl` directly
connects the shared L1I. Translation and fetch correlation belong to MMU and
frontend, not the physical cache. Keep the existing S0/S1/S2 timing and
simultaneous-flush/replacement contract when changing these boundaries.

Update the caller and its fault adaptation together; do not add architectural
fault fields to `InstructionCacheResult` or introduce a wiring-only wrapper.
See the [package graph](../../../rhodium/DEVELOPING.md) for dependencies.

Run `cores/rv5stage/tests/fetch-admission-test.rhm` through
`tools/run-racket-tests.sh`. The `rv5stage-instruction-memory-router`,
`rv5stage-fetch-throughput`, and `rv5stage-icache-coherence` rsim fixtures cover
ordered uncached work, replay, lineage, and dirty-code visibility. Run the
shared-cache fixtures for physical behavior, and `make check-boundaries` after
ownership or import changes.
