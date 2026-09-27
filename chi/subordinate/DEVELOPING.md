<!-- Guides changes to CHI subordinate engines and storage. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing CHI subordinate engines and storage

Read the [subordinate README](README.md) for the public contract and the
[parent guide](../DEVELOPING.md) for package-wide boundaries. This guide owns
engine, controller, and storage-backend maintenance.

## Architecture and ownership

Keep sequencing in shared engines and storage behavior in each backend. The
C++ DPI implementation lives in `dpi/`; platform register policy remains in
[`devices/`](../../devices/DEVELOPING.md). Directory boundaries add no RTL
hierarchy or per-directory facade.

## Implementation map

| File | Responsibility |
|---|---|
| [`subordinate-slots.rhdl`](subordinate-slots.rhdl) | Transaction occupancy, DBID association, and packet receipt |
| [`memory-controller.rhdl`](memory-controller.rhdl) | Shared configuration, native multibeat scheduling, and request checks |
| [`ram.rhdl`](ram.rhdl) | `SyncRam1RW` backend and compatibility re-exports |
| [`dpi-memory.rhdl`](dpi-memory.rhdl), [`dpi/chi_memory.cc`](dpi/chi_memory.cc) | DPI bridge and bounded sparse native byte store |
| [`single-beat-subordinate.rhdl`](single-beat-subordinate.rhdl) | Common one-outstanding MMIO phases and responses |

## Change workflow

### Memory transactions and backends

`CHISNTransactionSlots` updates receipt masks with independent indexed register
writes. Allocation selects a free slot in current occupancy; accepted DAT
selects an occupied slot, so the writes cannot collide. Do not introduce
same-cycle occupancy bypass without revisiting that invariant.

`memory-controller.rhdl` owns the common `CHIRamConfig`, `CHIRamParams`,
`CHIRamIdentity`, operation/completion payloads, and `build_chi_ram_controller`.
`ram.rhdl` owns only the `SyncRam1RW` backend and re-exports shared bindings
for existing importers. `dpi-memory.rhdl` imports the controller directly and
owns the DPI ABI, access enable/reset policy, and model-status assertion.
`dpi/chi_memory.{h,cc}` owns the bounded sparse byte store shared by DPI and
native simulation adapters. Its process-local model ID is independent of the
hardware NodeID. The reset-only initialization DPI supplies physical base and
capacity and identifies the instance through its DPI scope. Repeated reset
registration must preserve identity, range, and bytes. Invalid registrations
poison the registry so a host consumer cannot silently fall back after a failure.
The native registry freezes before image loading; access requires successful
initialization. Reset suppresses transactions without erasing memory.
The store and registry have no FESVR dependency.
The facade imports shared configuration from its owner, not through SRAM.

Keep the controller as an elaboration helper, not a wrapper circuit or a new
public memory protocol. Its backend factory runs once in the caller's module
and returns the binder for nonstallable issue/completion flows. Preserve the
operation metadata and ordering on every completion. Both current backends
complete one cycle after issue. Independent, non-pipelined read and write
`CompletionQueue` lanes own registered capacity reservations and isolate DAT
backpressure from RSP progress. Non-pipelined operation and DBID queues prevent
response readiness from propagating into CHI request admission. The shared
scheduler fairly selects one backend issue per cycle. Transaction slots accept
simultaneous read and write retirements. Storage and DPI policy remain
backend-owned.
Static configuration checks run in the shared constructors; request alignment,
range, mask, and poison assertions remain runtime controller checks. Do not
repeat constructor invariants in either concrete memory circuit.

RAM range checks use the protocol-neutral `transfer_in_range` helper from
`rhodium/std/interconnect.rhdl`. It widens exclusive ends, allowing a valid
window and transfer to end exactly at the address-space boundary. Keep size
and alignment policy in the controller.

### Single-beat devices and event ownership

`CHISingleBeatSubordinate` owns the shared five-phase MMIO transaction lifetime.
Its port remains `CHISNChannels`; devices forward their native port directly.
Device policy supplies request acceptance, the combinational read snapshot,
extra write legality, and write readiness. The engine's acceptance pulses are
edge events, not a second memory protocol. Do not feed acceptance back into its
own readiness predicate. Devices decode the incoming request for reads and the
retained request for writes. Read side effects occur on request acceptance;
write side effects occur on DAT acceptance, never on completion acceptance.
Snapshot storage and all responses are engine-owned. Keep register masks,
read-to-clear effects, interrupt state, and FIFO backpressure device-owned.

Boot-address, ACLINT, PLIC, and UART16550 all use this engine. Preserve their
existing gating versus assertion-only mask policies; sharing sequencing is
not permission to strengthen protocol checks. BootROM's multibeat read engine
and RAM's queued transactions are separate.

The engine's intrinsic `responses` tracing contract connects one input to both
outputs using the named `request` retained scope. Capture on `request_fire`, not unqualified REQ
fire; release on final RSP or read DAT, never DBID or incoming write DAT. Keep
these metadata declarations beside the production phase controls.

## Focused validation

Keep host tests and authoring fixtures in [`../tests/`](../tests/), and
behavioral benches in [`../tests/circt/`](../tests/circt/).

- Slot/controller changes: RAM configuration and DPI ABI host tests,
  `chi-ram` including simultaneous allocation/DAT, incomplete reset, and the
  expected invalid-request assertion; native DPI memory; Mini/Single SoC smoke.
  Test public behavior, not incidental hierarchy or instance counts.
- Range handling: RAM and BootROM top-of-address-space windows, plus the
  standard-library `transfer-range` arithmetic fixture.
- Single-beat sequencing: boot-address, ACLINT, PLIC, and UART simulations;
  boot-address negatives cover association and early DAT.
- Event ownership: `event-subordinate` compares the production engine's
  external occurrence graph across reused IDs, credit returns, write stalls,
  response stalls, and reset in all four non-idle phases. Run it with device
  simulations, then retry SingleCoreRV5StageSoC instrumentation for composed
  network coverage.

For source moves, update direct consumers, docs, and build/CI paths together.
Run `make check-boundaries` after module or dependency changes and use the
repository wrappers for affected host and behavioral checks.
