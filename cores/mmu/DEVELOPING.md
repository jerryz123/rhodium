<!-- Defines shared TLB/PTW implementation ownership, migration invariants, and focused regressions. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing shared translation

Read [README.md](README.md) for the public timing, permissions, cancellation,
and memory-response contract. Follow the [core ownership rules](../DEVELOPING.md)
and [package dependency graph](../../rhodium/DEVELOPING.md).

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
| `context.rhdl` | Stateless host/guest lookup construction from caller-selected effective controls |
| `udb.rhm` | Bare/Sv39 and guest translation modes and identifier capacities, excluding pipeline fault-precision claims |
| `vector.rhdl` | Core-neutral range certificates, context identity, and page-probe interfaces |
| `vector-certificate.rhdl` | Fast-proof PMA/translation checks, certificate construction, and context/epoch validation |
| `vector-window.rhdl` | Two-page certification and pinned mappings; failed proof selects precise fallback |
| `tlb.rhdl` | Associative bank, current permission checks, probes, refill, and host-port adapter |
| `walker.rhdl` | Host/nested traversal, PTE validation, saved VS frame, cancel/drain, host-port adapter |
| `tests/translation-service.rhdl` | Test-only serialized driver joining lookup and walking |

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
- Vector fallback probes use only the window's registered state and captured
  range. Do not restore admission-to-probe bypass: a live descriptor offer may
  itself depend on scalar WB/MMU acceptance. Preserve the independent MEM
  certificate path and allow an immediate response to a registered probe.
- `VectorCertificate` owns only the epoch register and combinational proof.
  Its Bare specialization uses the full 64-bit identity address for PMA checks
  before extracting PPN, emits zero/nonvirtual context, and compares only
  candidate epochs. Do not strengthen Bare validation with unused context bits.
  Preserve demand-accurate A/D permission results, not relaxed prefetch permission.
  RV5Stage selects an idle demand lookup; RV2Wide uses its independent probe.
  Keep those arbitration policies and normal-versus-explicit-guest context
  selection in the callers. Do not add buffering or gate candidate comparison
  with invalidation before the epoch edge.
- Keep the `mmu/walk` trace label and retained ownership contract stable.
  Residency excludes Idle and Drain; cancellation ends the architectural walk
  occurrence but does not erase physical response ownership.

Migrate callers and direct fixtures with public API changes. Update the package
dependency inventory when imports change. Keep generated RTL and trace headers
untracked; do not add compatibility wrappers under a named core.

## Validation

The shared fixtures live in `cores/mmu/tests/` and belong to `cores-components`:

```sh
tools/run-racket-tests.sh cores/mmu/tests/mmu-test.rhm cores/rv5stage/tests/mmu-test.rhm
FIXTURES='riscv-guest-translation riscv-nested-walker riscv-svnapot riscv-svpbmt' python3 tools/testing/rsim/run.py
FIXTURES='rv5stage-mmu-replay' python3 tools/testing/rsim/run.py
FIXTURES='riscv-walk-trace' python3 tools/testing/rsim/run.py
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
vector certificates. Use RV5Stage's [MMU guide](../rv5stage/mmu/DEVELOPING.md)
for broader pipeline/guest-fault changes. Test migration does not expand CI's
software matrix or publish another core's ISA profile.

For window timing changes, also run `rv2wide-mmu`. Its real-MMU fixture qualifies
page-check admission with older scalar request readiness, preserving the feedback
topology used by ordered WB admission. It checks rejection before capture,
registered warm-hit probing, demand contention, immutable ranges, and held
responses. Independent mock-memory responders cannot detect that feedback loop.
Fast-certificate changes use `rv5stage-mmu-replay` and `rv2wide-mmu` for
permission/PMA rejection, context and epoch changes, and lookup contention.
Also run `rv2wide-bare-memory` for identity proofs, high-address rejection,
unused candidate context, and pre-edge versus post-edge invalidation semantics.
