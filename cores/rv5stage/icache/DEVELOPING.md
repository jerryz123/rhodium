<!-- Guides contributors through implementing and validating RV5Stage's instruction cache. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the RV5Stage instruction cache

Read the L1I [README](README.md) for its public request/response, hit, refill,
replacement, flush, invalidation and deliberate-limit contracts. This
guide owns implementation placement and contributor validation.

## Architecture and ownership

The L1I package owns the instruction-access protocol, synchronous arrays,
lookup pipeline, replay outcomes, clean-line replacement, local flush and
invalidation behavior. The parent core owns virtual
translation, Fetch correlation, `FENCE.I` serialization, physical-region
checks, and the external CHI boundary.

Keep L1I independent of L1D. Reuse the parent cache parameters and sibling CHI
package's refill engines rather than importing the data-cache
package. [`../../check-boundaries.sh`](../../check-boundaries.sh) enforces that
separation.

## Implementation map

| Concern | Owner |
|---|---|
| Core-facing request and response bundles | [`protocol.rhdl`](protocol.rhdl) |
| Demand-priority lookup, best-effort prefetch admission, arrays, S2 outcomes, refill installation, replacement, flush, invalidation | [`cache.rhdl`](cache.rhdl) |
| Shared cache geometry | [`../cache.rhdl`](../cache.rhdl) |
| Complete-line RAM/ROM reads with retained region mode | [`../chi/line-read.rhdl`](../chi/line-read.rhdl) |
| Core/MMU/CHI integration | [`../rv5stage.rhdl`](../rv5stage.rhdl) |
| Host configuration and public protocol coverage | [`../tests/icache-test.rhm`](../tests/icache-test.rhm) |
| CIRCT/Verilator fixture | [`../../../tests/backend/`](../../../tests/backend/DEVELOPING.md#fixture-and-artifact-ownership) |

## Change the cache

1. Preserve fixed-latency S1-to-S2 Valid outcomes. Response storage and replay
   belong to the frontend, not the cache. Keep S0 virtual SRAM admission
   independent of S1 physical resolution. Pair a surviving physical request
   with the preceding read; kill younger S1 reads on frontend replay.
   Geometry rejection belongs to `../profile.rhm`.
2. S1 compares the returned tags with the translated address; S2 always captures
   the selected word, hit decision, and refill context. Keep these fixed-latency
   stages distinct from frontend replay and the blocking refill path. A miss returns replay while accepted refill work continues independently;
   retain a demand read error until a matching retry consumes one access fault.
3. Publish tag and validity only after the final installation word so a
   partial line cannot hit or satisfy a snoop.
4. Keep speculative `flush` separate from architectural `invalidate_all`,
   including their different treatment of resident and in-flight refill state.
5. Keep lookup and refill installation mutually exclusive on SRAM ports. Preserve
   accepted CHI transaction ownership through flush and architectural invalidation.
6. Keep a prefetch response-free and lower priority than a simultaneous demand;
   once admitted, reuse the ordinary coherent refill and installation path.
7. Update [README.md](README.md) when ports, timing, geometry, coherence,
   replacement, or deliberate limits change.

## Focused validation

The `rv5stage-icache-coherence` and `rv5stage-icache-coherence-flat`
fixtures connect real I/D caches to the inclusive and noncaching Homes. They
check dirty-code visibility after instruction invalidation and retention under
outer replacement. The standalone `rv5stage-icache` bench checks retry,
backpressure, read errors, and cancellation before and during installation,
including instruction-only cacheable ROM reads with no CompAck.

Use the host check for geometry and public protocol contracts:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/icache-test.rhm
```

Test cache snapshots, refill, invalidation, and access-error behavior in the compiled
`rv5stage-icache` fixture. Use the parent
[`DEVELOPING.md`](../DEVELOPING.md#focused-validation) when a change crosses
refill, snoop, MMU, Fetch, or complete-core integration, and use the backend test
[`DEVELOPING.md`](../../../tests/backend/DEVELOPING.md) for CIRCT and Verilator
modes. Repository wrappers provide a fresh compiled root.
