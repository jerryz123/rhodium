<!-- Guides contributors through implementing and validating RV5Stage's instruction cache. -->

# Developing the RV5Stage instruction cache

Read the L1I [README](README.md) for its public request/response, hit, refill,
replacement, flush, invalidation, snoop, and deliberate-limit contracts. This
guide owns implementation placement and contributor validation.

## Architecture and ownership

The L1I package owns the instruction-access protocol, synchronous arrays,
lookup pipeline, response buffering, clean-line replacement, local flush and
invalidation behavior, and clean snoop response. The parent core owns virtual
translation, Fetch correlation, `FENCE.I` serialization, physical-region
checks, and the external CHI boundary.

Keep L1I independent of L1D. Reuse the parent cache parameters and sibling CHI
package's refill and clean-snoop engines rather than importing the data-cache
package. [`../../check-boundaries.sh`](../../check-boundaries.sh) enforces that
separation.

## Implementation map

| Concern | Owner |
|---|---|
| Core-facing request and response bundles | [`protocol.rhdl`](protocol.rhdl) |
| Demand-priority lookup, best-effort prefetch admission, arrays, buffering, refill installation, replacement, flush, invalidation, and snoop arbitration | [`cache.rhdl`](cache.rhdl) |
| Shared cache geometry | [`../cache.rhdl`](../cache.rhdl) |
| Retry-aware complete-line refill | [`../chi/refill.rhdl`](../chi/refill.rhdl) |
| Clean snoop transaction lifetime | [`../chi/snoop.rhdl`](../chi/snoop.rhdl) |
| Core/MMU/CHI integration | [`../rv5stage.rhdl`](../rv5stage.rhdl) |
| Host configuration and public protocol coverage | [`../tests/icache-test.rhm`](../tests/icache-test.rhm) |
| CIRCT/Verilator fixture | [`../../../tests/backend/`](../../../tests/backend/DEVELOPING.md#fixture-and-artifact-ownership) |

## Change the cache

1. Preserve the ordered Decoupled-to-Irrevocable protocol and reserve response
   capacity before accepting a request.
2. Keep a hit's one-stage lookup and response-queue timing distinct from the
   blocking refill path.
3. Publish tag, state, and validity only after the final installation word so a
   partial line cannot hit or satisfy a snoop.
4. Keep speculative `flush` separate from architectural `invalidate_all`,
   including their different treatment of resident and in-flight refill state.
5. Preserve SRAM ownership priority among lookup, snoop, and refill
   installation, and keep every CHI response stable until accepted.
6. Keep a prefetch response-free and lower priority than a simultaneous demand;
   once admitted, reuse the ordinary coherent refill and installation path.
7. Update [README.md](README.md) when ports, timing, geometry, coherence,
   replacement, or deliberate limits change.

## Focused validation

Use the host check for geometry and public protocol contracts:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/icache-test.rhm
```

Test cache state, refill, invalidation, and snoop behavior in the compiled
`rv5stage-icache` fixture. Use the parent
[`DEVELOPING.md`](../DEVELOPING.md#focused-validation) when a change crosses
refill, snoop, MMU, Fetch, or complete-core integration, and use the backend test
[`DEVELOPING.md`](../../../tests/backend/DEVELOPING.md) for CIRCT and Verilator
modes. Repository wrappers provide a fresh compiled root.
