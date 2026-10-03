<!-- Guides maintenance of target-specific RTL emission and its semantic regression suites. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing hardware backends

Read [README.md](README.md) for imports, supported operations, representation,
DPI ABI, and deliberate limits. This guide owns implementation boundaries,
extension workflow, and validation. The [package graph](../DEVELOPING.md) owns
allowed imports; [test maintenance](../../tools/testing/DEVELOPING.md) owns CI
and generated-artifact policy.

## Architecture and ownership

`compile_program` selects a target. Each backend uses `RTLTarget`: ordinary
preparation calls the shared `prepare_rtl` helper to obtain a fresh, verified
reachable graph and manifest, then constructs its emission plan. The same
target's `.plan(prepared)` accepts an existing `PreparedRTL` without preparing
again, for composition inside another target. Both paths emit one artifact.
Compilation owns requests/results; lowering owns copying and portable
expansion; each backend owns its representation. Frontend and Flow libraries
need no backend-specific branches.

The concrete emitters consume public core IR and cannot import frontend,
compilation, or analysis policy. Only target adapters import compilation.
`emit_circt`, `emit_module_circt`, and `emit_verilog` are internal implementation
utilities for targets and focused backend tests. Production drivers use an
explicit target and top through `compile_program`; there is no whole-inventory
program compilation API.

CIRCT emission produces MLIR; the external CIRCT pipeline owns optimization,
sequential/memory lowering, and SV export. Direct emission owns the complete SV
artifact without loading CIRCT. Keep representation helpers backend-local unless
a shared semantic responsibility actually belongs in core.

## Implementation map

| File | Responsibility |
|---|---|
| `circt-target.rhm`, `verilog-target.rhm` | RTL targets with shared ordinary/prepared plan construction |
| `circt.rhm` | CIRCT types, aliases, operation lowering, and textual MLIR |
| `verilog.rhm` | Opcode inventory, packed types, names, nets, state, and SV rendering |
| `tests/*-test.rhm` | Host contracts, prepared graph reuse, invalid uses, determinism, and source preservation |
| `tests/verilog/fixtures.rhm` | Builder-owned semantic fixtures shared by both backends |
| `tests/verilog/emit-{direct,circt}.rhm` | Explicit-target fixture emission |
| `tests/verilog/support.py` | Tool execution, logs, and packed stimulus tables |
| `tests/verilog/scalar_cases.py`, `state_cases.py` | Independent integer/state oracles |
| `tests/verilog/rtl.py` | Combined scalar, aggregate, hierarchy, and state bench |
| `tests/verilog/memory.py` | Synchronous-memory oracle and isolated per-width simulations |
| `tests/verilog/assertions.py`, `dpi.py` | Assertion and foreign-effect families |
| `tests/verilog/run.py` | Family selection and optional CIRCT comparison |
| `tests/verilog/run-integration.py` | Existing SyncRam and UART benches/native model |

### Representation invariants

Direct SV gives each IR result a width-constrained net. This preserves modular
arithmetic before later widening; changes to expression inlining must retain
those boundaries. Signed operations opt in explicitly and shifts retain every
count bit. Packed layouts follow core record/array ordering. Projected drives
are already materialized by preparation and must not produce duplicate drivers.

`SvTypes` is local to one artifact and owns an ordered shared type package.
Reserve authored scope and member names before allocating aliases. Physical
shape identity ignores preferred names; equal shapes reuse the first alias.
Fully qualified types prevent local names from hiding typedefs. Internal net
names use module-local traversal order, independently of global IR IDs. Allocate
references before rendering statements so forward-driven wires remain legal.

CIRCT shape/render caches also belong to one emission. Collect aliases before
rendering types, index aliases by canonical shape, and preserve first-seen
naming/order. Standalone module emission has an empty alias environment. Never
reuse rendered types between those environments.

Register, memory, and DPI result updates use nonblocking assignments so other
edge-triggered consumers see pre-edge values. DPI calls receive blocking private
temporaries before publishing result state. Never reorder or merge observable
foreign effects. Each memory has one procedural write owner; separate valid
writes retain simultaneous semantics. Core owns collision and invalid-address
preconditions, and the emitter must not invent priorities or defined values.

Keep assertion sampling synchronous: condition, guard, and reset are sampled
together before state updates. `SYNTHESIS` removes assertions but must reject
live DPI, whose functional effects cannot be discarded. CDC attributes attach
only to verified synchronizer stages; clocking analysis stays a separate target.

The CIRCT adapter folds a one-bit whole-word mask into write enables because
CIRCT 1.155 omits that mask from generated memory helpers. On shared ports,
select the masked enable only in write mode so zero-mask reads remain enabled.
The focused regression belongs in `tests/sync-memory-masked-test.rhm`; both
backend memory oracles cover the runtime behavior.

## Change workflow

1. Confirm the verified core meaning and public representation contract.
2. Add or adjust the owning backend's lowering. Keep target adapters thin and
   avoid adding frontend-specific decisions or broadening undefined behavior.
3. Update the explicit direct opcode inventory when core schemas change. All
   live operations must be supported before an artifact is published.
4. Add the smallest host or behavioral regression that distinguishes the change.
   Use an independent oracle rather than copying the emitted implementation.
5. Update the README support contract and this guide's implementation map when
   responsibilities move. Run boundary checks after import/module changes.

Do not add tests merely asserting that an unimplemented feature is absent.
Invalid type/operand combinations of supported operations are valid host cases.
Keep generated RTL, DPI headers, executables, and stimulus tables temporary.
Canonical example goldens are owned by the [CIRCT guide](../../tools/testing/circt/DEVELOPING.md#verilog-references),
not by the direct runner. Explain intentional output changes before updating any
checked-in reference.

## Validation

All Racket/Rhombus commands use repository wrappers and their worktree-owned
cache. The root targets below already do so.

| Scope | Command |
|---|---|
| Compiler/backend host contracts | `make backend-test` |
| Direct SV and authored integrations, without CIRCT | `make verilog-test` |
| Both backends against oracles and each other | `make backend-differential-test` |
| One family | `python3 rhodium/backend/tests/verilog/run.py --family sync-memory --differential` |
| One authored integration | `python3 rhodium/backend/tests/verilog/run-integration.py --fixture uart-dpi --differential` |

`--family` accepts `rtl`, `assertions`, `sync-memory`, and `dpi`, may be repeated,
and defaults to all. `--differential` additionally requires the pinned CIRCT
executable (`CIRCT_OPT`, PATH, or repository installation). Both routes use
Verilator with runtime assertions; direct-only execution must not require CIRCT.
Failed runs retain their temporary logs/artifacts; successful runs remove them.

### Behavioral coverage

| Family | Required distinctions |
|---|---|
| RTL | Widths 1/5/65; modular arithmetic and overshifts; hierarchy/portable expansion; packed layouts/casts; dynamic vectors and write sets; partial outputs; reset/hold/edge timing; asynchronous memories; CDC stage latency |
| Synchronous memory | Depths 1/3/4; 1R/1W/1R1W/1RW; scalar/aggregate data; bit, granule, and whole-word masks; per-bit definedness; old read results; independent instances |
| Assertions | Guard/reset suppression; pre-update sampling; occurrence/label diagnostics; two passing and six expected failures; all eight scenarios with synthesis defined |
| DPI | Native and packed widths through 129 bits; wide inputs/out results; held results; independent clocks/enables; call multisets and instance scopes; generated C-header ABI; explicit synthesis rejection |
| Authored integrations | Existing SyncRam timing/masks and UART bidirectional serial/PTY behavior, unchanged package-owned benches and production native model |

Only defined output bits enter comparisons or differential transcripts. Oracles
track startup and partial initialization explicitly; undefined addresses,
collisions, invalid write sets, and unconstrained decode bits do not gain a
promised value. Stimuli use temporary hex tables rather than enormous unrolled
SV benches. Wide transcripts are chunked to respect Verilator argument limits.

CIRCT memory helpers are isolated per artifact/width. The reference pipelines
lower high-level memories before sequential lowering and use memory simulation
with randomization disabled and undefined disabled-read results. Synchronous
memory benches run with `SYNTHESIS` defined. DPI runs separately because it
rejects that mode. The reference type fixture renames a `Packet` output to
`restored` to avoid the pinned CIRCT exporter's typedef-shadowing issue; the
direct fixture retains the collision and both share the same bit-level oracle.

Authored integrations compare logical signatures, module inventories, portable
lowering decisions, and source preservation. Their shared behavioral benches
are not complete cycle-trace equivalence proofs. The UART minimum-divider
fixture permits `UNSIGNED` for a legal constant comparison; other diagnostics
stay fatal. SoC smoke qualification and its additional warning allowances belong
to [simulation maintenance](../../sims/DEVELOPING.md).

After changes, run boundary, license, parameter-annotation, and whitespace audits
as applicable. CI runs the direct lane without CIRCT and the differential lane
with both tools. Keep dependency routing current when fixtures reuse another
package's circuit, bench, or native model.
