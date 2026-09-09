<!-- Guides changes to CHI subordinate engines and storage. -->

# Developing CHI subordinate engines and storage

Read [README.md](README.md) and the parent
[CHI developer guide](../DEVELOPING.md) before changing this area.
The parent owns package-wide boundaries; this guide owns component extension and validation.

Keep sequencing in the shared engines and storage behavior in its backend. The C++ DPI implementation lives in dpi/; platform register policy remains in devices/.

Keep tests and authoring fixtures in [`../tests/`](../tests/), and behavioral
benches in [`tests/backend/`](../../tests/backend/DEVELOPING.md).
For source moves, update direct consumers, package documentation, and build/CI
paths together. Run `make check-boundaries` and the affected host and behavioral
checks with a fresh isolated compiled root. Directory boundaries do not add RTL
hierarchy or per-directory facade modules.

## Extension and focused validation

`chi/subordinate/memory-controller.rhdl` owns the common `CHIRamConfig`, `CHIRamParams`,
`CHIRamIdentity`, operation/completion payloads, and `build_chi_ram_controller`.
`chi/subordinate/ram.rhdl` owns only the `SyncRam1RW` backend and re-exports the shared bindings
for existing importers. `chi/subordinate/dpi-memory.rhdl` imports the controller directly and
owns the DPI ABI, access enable/reset policy, and model-status assertion.
The facade imports shared configuration from its owner, not through SRAM.

Keep the controller as an elaboration helper, not a wrapper circuit or a new
public memory protocol. Its backend factory runs once in the caller's module
and returns the binder for nonstallable issue/completion flows. Preserve the
operation metadata and ordering on every completion. Both current backends
complete one cycle after issue; `CompletionQueue` owns capacity reservations
and downstream backpressure. Storage and DPI policy remain backend-owned.
Static configuration checks run in the shared constructors; request alignment,
range, mask, and poison assertions remain runtime controller checks. Do not
repeat constructor invariants in either concrete memory circuit.

For memory-controller changes, run the RAM configuration and DPI ABI host tests,
`chi-ram` simulation and its expected invalid-request assertion, the native DPI
memory test, and MiniSoC/SimpleSoC smoke tests. These cover the SRAM and DPI
consumers without adding tests of incidental hierarchy or internal instance counts.

RAM range checks use the protocol-neutral `transfer_in_range` helper from
`rhodium/std/interconnect.rhdl`. It widens exclusive ends, allowing a valid
window and transfer to end exactly at the address-space boundary. Keep size
and alignment policy in the controller. RAM and BootROM benches cover a
relocated top-of-address-space window; generic arithmetic coverage belongs to
the standard-library `transfer-range` fixture.

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
and RAM's queued transactions are separate. Run all four device simulations;
boot-address negatives also exercise the shared association/early-DAT checks.
