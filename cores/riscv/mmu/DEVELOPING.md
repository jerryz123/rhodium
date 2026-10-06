<!-- Defines shared TLB/PTW implementation ownership, migration invariants, and focused regressions. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing shared translation

Read [README.md](README.md) for the public timing, permissions, cancellation,
and memory-response contract. Follow the [core ownership rules](../../DEVELOPING.md)
and [package dependency graph](../../../rhodium/DEVELOPING.md).

## Architecture and ownership

This package may depend on public RISC-V architectural helpers and Rhodium/Flow,
never named-core configuration, payloads, vector state, or cache arbitration.
RV5Stage owns its integrating MMU; another core must supply its own miss and
retirement policy rather than import that integration.

Keep one TLB implementation for host and composed guest entries and one walker
with a saved VS continuation. The host adapters are projections, not alternate
hardware. Pure PTE geometry, permissions, and PBMT helpers stay in `riscv/rtl`;
entry storage, replacement, walk frames, and response ownership stay here.

## Implementation map

| File | Responsibility |
|---|---|
| `protocol.rhdl` | Host-only request, leaf result, and PTE-memory contracts |
| `translation.rhdl` | Host/guest lookup, mapping, fill, typed faults, PBMT-aware memory, host projections |
| `tlb.rhdl` | Associative bank, current permission checks, probes, refill, and host-port adapter |
| `walker.rhdl` | Host/nested traversal, PTE validation, saved VS frame, cancel/drain, host-port adapter |
| `../tests/translation-service.rhdl` | Test-only serialized driver joining lookup and walking |

## Change workflow

Preserve these invariants when extending the components:

- Permission results are not cached: demand hits recheck current controls.
  Probes ignore A/D, but must find the same allowed access class at both stages.
- Mapping page size is separate from traversal level. Normalize leaf PPNs;
  composed guest entries retain exact guest provenance at 4 KiB granularity.
- Capture all context at walk admission. Later live requests cannot change a
  pending PTE's permission checks, PBMT, or completion provenance.
- An accepted PTE owns its reply across cancellation and invalidation. Keep
  Drain separate from Idle; zero-latency responses are valid on acceptance.
- Invalidation wins over refill at the edge. Core-owned flush/retirement
  controls must not feed back through combinational demand-hit qualification.
- Keep the `mmu/walk` trace label and retained ownership contract stable.
  Residency excludes Idle and Drain; cancellation ends the architectural walk
  occurrence but does not erase physical response ownership.

Migrate callers and direct fixtures with public API changes. Update the package
dependency inventory when imports change. Keep generated RTL and trace headers
untracked; do not add compatibility wrappers under a named core.

## Validation

The shared fixtures live in `cores/riscv/tests/` and belong to `cores-components`:

```sh
tools/run-racket-tests.sh cores/riscv/tests/mmu-test.rhm cores/rv5stage/tests/mmu-test.rhm
FIXTURES='riscv-guest-translation riscv-nested-walker riscv-svnapot riscv-svpbmt riscv-walk-trace rv5stage-mmu-replay' bash tools/testing/circt/run.sh --simulate-only
make check-boundaries
```

Use the smallest affected subset. `riscv-guest-translation` covers cold/warm
translation, context identity, replacement, current permissions, fault
provenance, held results, cancellation, and invalidation. `riscv-nested-walker`
covers Bare/paged combinations, nested PTE sequences, captured controls, and
response draining. Svnapot/Svpbmt fixtures cover mapping geometry and page
attributes. `riscv-walk-trace` checks residency and PTE/completion ancestry using
public transfers, including faults, cancellation, and pending reset.

`rv5stage-mmu-replay` stays with RV5Stage: it checks production miss arbitration,
core/PTE response routing, useful work across fetch recovery, prefetch, and
vector certificates. Use RV5Stage's [MMU guide](../../rv5stage/mmu/DEVELOPING.md)
for broader pipeline/guest-fault changes. Test migration does not expand CI's
software matrix or publish another core's ISA profile.
