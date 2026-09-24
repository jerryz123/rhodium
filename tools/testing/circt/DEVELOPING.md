<!-- Explains CIRCT fixtures, event snapshot/stream tests, and Verilog references. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing CIRCT tests

Read the [CIRCT test guide](README.md) first for focused selectors, runner
modes, toolchain behavior, and failure interpretation. This guide owns fixture
and artifact structure, exact-reference maintenance, and changes to the runner.

The [backend package guide](../../../rhodium/backend/README.md) owns lowering
architecture and operation contracts. The
[example guide](../../../examples/README.md) owns the canonical example catalog.

## Architecture and implementation map

The manifest in [`run.sh`](run.sh) is authoritative for fixture
names, groups, example exports, direct emitters, Verilator tops, reference
eligibility, and expected failures.

| Path | Maintenance responsibility |
|---|---|
| [`run.sh`](run.sh) | Fixture manifest, selection, package-owned artifact resolution, CIRCT pipeline, exact diff, and Verilator orchestration |
| [`load-example.rkt`](load-example.rkt) | Materializing selected example exports in one process |
| `<package>/tests/circt/emit-*.rhm` | Package-owned direct MLIR integration shapes without an example-owned reference |
| `<package>/tests/circt/verilog/` | Package-owned behavioral benches and optional local DPI companions |
| [`examples/`](../../../examples/README.md) | Canonical designs and their exact Verilog-reference exports |

The [repository test-development guide](../DEVELOPING.md) owns test placement,
authoring principles, and CI classification outside this backend-specific
fixture boundary.

RV5Stage vector fixtures belong to `cores-vector-functional-1` or
`cores-vector-functional-2` for the default functional configurations, or
`cores-vector-configurations` for alternate XLEN, VLEN, queue depth, and slot
counts. The numbered functional groups partition one semantic owner by CI
runtime; `cores-vector-functional` combines them for local runs.
`cores-vector` also includes the configuration group, and `cores` combines
the vector aggregate with the component, scalar/frontend execution, memory,
and cache groups. CI gives the three vector leaves and the package-owned
HardFloat runner independent jobs.
The CI classifier check uses the runner's manifest-only listing to require
the two functional leaves to be nonempty, disjoint, and exhaustive.

Alternate vector fixtures cover parameter boundaries, not a cross-product of
every subsystem with every supported value. Keep one behavioral owner for each
distinct risk: RV32 packed transport, RV32 unroller geometry, a single
completion slot, wide mask indexing, and gather indices above 255. The default
functional fixtures own operation breadth. A second fixture should not rerun a
complete operation scoreboard merely to repeat a shared completion-slot or
XLEN value already covered at its owning boundary.

## Fixture and artifact ownership

Example-backed entries name an example module, a concrete design export, an
optional Verilator top, and either a Verilog-reference export or `-`. A named
reference marks a compact fixture whose readable generated output is part of
the example; `-` keeps integration-scale fixtures under lowering and behavioral
coverage without a large exact snapshot. The ordinary golden pair is `design`
and `verilog_reference`; additional designs use the same prefix for both
exports, such as `cast_design` and `cast_verilog_reference`. References live
beside their designs so reviewers can see the authoring input and generated
result together.

Direct `emit-*.rhm` fixtures own integration shapes that do not belong
to one canonical example. They print MLIR for CIRCT verification and may name
a Verilator top, but they do not own example Verilog references. Add or rename
either kind through the manifest. The runner resolves each declared direct
emitter and each selected `<fixture>_tb.sv` or optional `<fixture>_dpi.cpp`
from exactly one package-local `tests/circt/` directory; zero or multiple owners
are errors.
`make examples` and `make check-example-verilog` check every concrete design's
manifest coverage and validate only declared golden exports without running
CIRCT or Verilator.

Behavioral benches live under the owning package's `tests/circt/verilog/`.
An example fixture with a top uses `verilog/<fixture>_tb.sv`. The runner
automatically links a matching `verilog/<fixture>_dpi.cpp`; direct emitter
fixtures can additionally link a matching source from
[`devices/uart/dpi/`](../../../devices/uart/dpi/), with hyphens
in the fixture name changed to underscores. Assertion and protocol monitors
also have dedicated negative benches. Those checks pass only when simulation
fails and reports the expected assertion label, so an expected failure is not
treated as an unchecked crash.
The failure runner accepts optional trailing native sources for DPI-backed
assertion benches, such as the RHEG collector for missing selected parents.

The `event-runtime`, `event-pipeline`, `event-elastic`, `event-queue`, `event-arbiter`, `event-demux`, `event-atomic-fork`, `event-broadcast`, `event-join`, `event-stall`, and `event-offer` direct fixtures
additionally link the independent RHEG collector implementation. Each local DPI companion is a transfer scoreboard,
not a second implementation of the collector or ABI.
`event-instance` checks nested module-local runtime identities on repeated
module definitions, comparing graph registrations and lineage with public
transfers. It covers pre-use identity changes, late first activity, equal IDs in
distinct instances, 64-bit values, and reset rebinding. Its negative bench must
fail the stability assertion when an already-bound identity changes.
`event-vector` instruments the production vector execution engine. Its
public-transfer oracle tracks sequence/issue occurrences,
fixed-cycle feedback, accepted slots, tagged returns, and ordered drain without
reading generated metadata state. Equal-PC macros, retries after a prefix,
fault/truncation, slot reuse, out-of-order returns, empty/store completions,
stalls, and pending reset protect vector milestones. Its sequencing-stall checks
require an active sequencer owner and at least one blocked setup, source, or
aggregate operand-fetch acceptance; transferred plans must have no blocked
reason. The feedback oracle checks public sequencing release: final read-plan
transfer for ordinary compute and final authorization, fault, truncation, or
cancellation for serialized work. Same-cycle replacement releases the old
sequencer owner before capturing the new one; the oracle retains issue owners
until their separate issue-completion pulses. Retry keeps its existing owner,
and accepted results may outlive sequencing ownership.
`rv5stage-vector-config` additionally instruments its existing real-core
program and checks each sequencing occurrence against its scalar WB ancestor,
while retaining its architectural signatures and exact VRF-write scoreboard.
`event-window` additionally checks retained multi-entry contributions through
downstream elastic storage against a public fill/release model. `event-frontend`
runs the existing fetch bench against an instrumented production frontend;
its public memory/instruction scoreboard checks exact occurrence parents and
captures, not generated controls or payload-equality matching.
`event-home` runs the inclusive-Home bench with its production retained contract
and caller-owned checkpoints in `chi/tests/home-trace-fixture.rhdl`. It checks
every emitted request/response/data occurrence against public port
transfers, with reused IDs, hit/miss responses, stalls, and pending reset.
`event-subordinate` checks intrinsic retained-request contracts on the shared
single-beat MMIO engine. Its public-transfer scoreboard requires exact parents
through DBID and delayed write data, ignores credit returns, and tests reset
in every retained phase plus read/write completion under backpressure.
`event-fesvr` instruments the production host access engine under the existing
MMIO bench. An independent public-transfer scoreboard checks every request
fragment, write-data transfer, and final host response against its accepted
command, including errors, delayed completion, and pending reset. The ordinary
and traced emitters share `sims/tests/fesvr-mmio-fixture.rhdl` service parameters.
`event-feedback` checks a queue/grant recirculation loop without an internal
checkpoint. Its public-control FIFO scoreboard preserves exact parent references
across multiple laps, repeated payloads, stalls, simultaneous transfers, and reset;
an uninstrumented lane checks functional equivalence. Host diagnostics cover
same-cycle cycles and unbounded parent accumulation.
`event-partial` checks opaque storage beside known and unannotated sources through
grant selection, queues, flow-through shift queues, a join, and a downstream checkpoint. Its independent
transfer scoreboard compares exact edges and unknown flags under changing grants,
stalls, equal payloads, and pending reset; a second lane checks unchanged RTL
behavior. Host tests retain strict rejection and unsafe-fanout diagnostics.
`event-branching` extends this coverage through direct/configured 3x3 crossbars
with two fresh sources, a feedback queue, and two buffered exits. Its independent
FIFO model checks every exact parent occurrence, all source-to-exit routes,
repeated feedback, changed grants under stall, concurrent exits, and pending reset.
Keep uncertified feedback fanout under host rejection coverage; do not weaken
branching validation to make a cyclic fixture elaborate.
Pipeline, elastic, queue, arbiter, and broadcast coverage includes direct
self-described modules and configured adapters, with independent transaction
scoreboards and uninstrumented reference lanes. `event-instrument-test.rhm`
also checks named local regions, module/inline ownership equivalence, wrapper
delegation through checkpoints, and conflicting/foreign-control rejection.
The `rv5stage-load-hit` fixture also links RHEG and exports a matching descriptor;
its scoreboard checks D-cache S1/MEM and S2/WB alignment, public core/cache
admission, and retained S2-to-S3-to-S4 ancestry including direct-refill fields.
The existing bench checks functional load timing and architectural results.
`event-parents` checks qualified original/intermediate checkpoints, late-bound WB,
and selected intermediate/combined parents past WB through one flushed pipe and
its ordinary consumer fork. An unannotated pipe checks unchanged functional valid
and payload. Its public-input scoreboard uses repeated payloads, bubbles, and reset
and compares every occurrence and edge. Host coverage also retains complementary
filter/selection paths, rejects bad references, rebinding, conditional binding,
and uncertified fanout, and checks module-occurrence identities and metadata-only binding.
The negative bench forces a child observation without its qualified upstream
checkpoint and requires the runtime missing-parent assertion.
Every refill receives RetryAck and PCrdGrant before retransmission, with request
backpressure; both attempts must retain the same refill residency, whose parent
is the original S4 occurrence.
`rv5stage-fetch-throughput` similarly links RHEG for the real frontend/MMU/L1I
path. A public admission/S1-kill/S2-outcome model identifies the exact S0 parent
of refill residency and its TXREQ descendants, including delayed retries, request backpressure, redirect while
the refill remains owned, and pending reset. Retain its cold/warm instruction
throughput and payload checks alongside the lineage scoreboard.
`rv5stage-fetch-source` independently models the original cursor, continuation,
admission, and replacement priorities from public inputs. It compares inactive
offer payloads as well as transfers, and checks exact restart/replay/successor/
held parent occurrences with equal PCs, blocked replacements, clears, and reset.
`rv5stage-fetch-prediction` instruments its existing production frontend fixture.
Its public request/kill pipeline model identifies the precise S2 occurrence that
triggers each selected fallback, then checks its S0 parent reference through the
redirect pipe and blocked cursor. Direct/compressed jumps, returns, straddling
instructions, and held redirects retain the existing functional scoreboard.
The same monitor checks ordinary S1-selected successors against their original
S0 occurrence, including BTB-predicted and sequential requests.
Core CMO/WRS and FP/scalar regressions check the inline live/maintenance/WRS
retirement flows, completion policy, and dispatch behavior at the core boundary.
`event-offer-register` compares traced/untraced public outputs and exact captured
owners through stalled replacement, simultaneous update/delivery, pending stall
observations, drain, and reset. Equal payloads must not merge owner identities.
`event-retained` independently covers repeated output, release/replacement,
pending reset, and unknown traffic selected beside traced traffic.
Its scoped relation ends at an opaque child's attempt output before a normal
Flow mapper. Host coverage also preserves a downstream checkpoint, permits an
unrelated traced lane, and rejects summaries overlapping existing Flow.
`rv5stage-compack` instruments both production line engines. Its independent
public-transfer packet-set model checks exact RXDAT-to-CompAck parents with
reordered/gapped packets, repeated IDs and payloads, request/acknowledgement/
completion stalls, reset during collection and pending acknowledgement, and
noncoherent instruction ROM reads interleaved with coherent requests.
It also compares exact refill start/end cycles against command/completion transfers.
`rv5stage-copyback` retains its packet and coherence checks while validating
15 residency intervals across all three DAT widths and retries.
`rv5stage-walk-trace` checks the production walker's exact residency graph,
PTE/completion parents, held completion, faults, cancellation, and pending reset
using only public handshakes.
`event-crossbar` links RHEG and checks direct/configured grant-controlled
crossbars with flow-through input queues. Its independent FIFO/transfer model
checks exact parent occurrence IDs for equal payloads, all routes, simultaneous
outputs, zero grants, changing stalled selections, full replacement, and reset.

An emitter may additionally export `event_manifest_cpp`, generated from the
same instrumented elaboration it prints. `load-example.rkt` writes this string
to `<fixture>_manifest.h` beside temporary MLIR. The runner supplies that directory
and the event runtime include directory to the C++ compiler. The join fixture
uses this path to bind its manifest before callbacks and export a validated
snapshot. These headers are generated artifacts, never checked-in references.
`rheg/tests/run-event-collector.sh` owns standalone C++ contract tests and is also invoked
by the `event-runtime` simulation fixture.
The optional `rheg/tests/run-event-perfetto.sh` builds `event-stream-test.cpp`,
`event-perfetto-test.cpp`, and the standalone converter, checks live/replay
parity, and queries the native Perfetto importer without Python. See the
[stream validation guide](../../../rheg/DEVELOPING.md#focused-validation).
This compatibility test is separate from default CIRCT simulation so Perfetto
does not become a simulator build dependency.

MLIR, generated SystemVerilog, Verilator object directories, and logs are
created in a temporary `/tmp/rhodium-circt.*` directory and removed when the
runner exits. They are diagnostic artifacts, not checked-in outputs. The only
source-writing mode is the intentional golden update described below.

## Verilog references

The [CIRCT test guide](README.md#toolchain-and-exact-reference-behavior) owns
the exact-output contract, normalization boundary, tool discovery, and
alternate-version behavior. This section covers maintaining those references.

When a backend change intentionally changes generated SystemVerilog, update
only the affected reference and review the example-source diff:

```sh
FIXTURE=bundle make update-verilog-goldens
```

Without `FIXTURE`, that target rewrites every example-owned reference. The
update mode uses whichever `circt-opt` the runner resolves and does not reject
an alternate version, so use the pinned toolchain unless the version transition
itself is intentional. Never use golden updates merely to make an unexplained
diff disappear.

## Change workflows

### Add an example-backed fixture

1. Keep the canonical design and its `verilog_reference` export together in
   the owning example source.
2. Add the fixture, group, export names, optional top, and expected behavior to
   the manifest in [`run.sh`](run.sh).
3. If simulation is required, add
   `<package>/tests/circt/verilog/<fixture>_tb.sv` and an optional matching
   `<fixture>_dpi.cpp` beside it.
4. Run `make check-example-verilog`, then the narrow selector from the
   [CIRCT test guide](README.md#choose-the-smallest-useful-run).
5. If the reference changed intentionally, update only that fixture with the
   pinned CIRCT tool and review the example-source diff.

### Add a direct emitter

1. Add `<package>/tests/circt/emit-<fixture>.rhm` for an integration shape that
   does not belong to a canonical example.
2. Declare it in the manifest; do not rely on filename discovery.
3. Add a matching bench only when the fixture needs behavioral validation.
   A direct emitter may use a local DPI companion or the fixture-name-matched
   source under [`devices/uart/dpi/`](../../../devices/uart/dpi/).
4. Run the fixture first in `--verify-only` mode, then add simulation if the
   change has a runtime contract.

### Change or rename a fixture

Update the manifest, example exports or emitter, bench and DPI filenames, and
any expected-assertion label as one change. Re-run manifest coverage before the
focused external check. Do not check in generated MLIR, SystemVerilog,
Verilator object directories, or runner logs.
