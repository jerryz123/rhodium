<!-- Guides contributors through RV5Stage implementation ownership, diagrams, and validation. -->

# Developing RV5Stage

Read the core [README](README.md) for the pipeline, completion, ordering,
system, memory, privileged-state, and deliberate-limit contracts. This guide
owns implementation placement, change sequencing, generated diagrams, and
focused validation.

## Architecture and dependency boundary

RV5Stage may depend on public Rhodium libraries, the pure RISC-V model and RTL
adapter, reusable components directly under `cores/`, HardFloat, and shared
CHI libraries. It must not import another named core, a backend, examples, or
tests. The parent [`check-boundaries.sh`](../check-boundaries.sh) enforces these
rules plus decode-column and cache-package separation.

Keep the scalar pipeline dependent on the RV5Stage cache protocols rather than
a generic memory transport. Keep I-cache and D-cache packages independent of
each other; share external transaction machinery through the CHI package.

## Implementation map

| Area | Ownership |
|---|---|
| [`profile.rhm`](profile.rhm) | Immutable ISA, MMU, and cache specialization description |
| [`rv5stage.rhdl`](rv5stage.rhdl) | Core, MMU, cache, uncached, and CHI composition |
| [`core.rhdl`](core.rhdl) | Scalar pipeline, forwarding, hazards, commit, and deferred completion |
| [`bundles.rhdl`](bundles.rhdl) | Scalar pipeline payloads |
| [`fetch.rhdl`](fetch.rhdl) | Aligned-word window, configured compressed-profile expansion, instruction queue, and redirect flushing |
| [`decode/DEVELOPING.md`](decode/DEVELOPING.md) | Structured integer and FP control generation |
| [`register-file.rhdl`](register-file.rhdl) | Two-read, two-write integer register bank |
| [`fp/DEVELOPING.md`](fp/DEVELOPING.md) | FP payloads, register state, execution lanes, LSU bridges, and completion |
| [`csr.rhdl`](csr.rhdl), [`interrupt.rhdl`](interrupt.rhdl) | Privileged state, traps, counters, and interrupts |
| [`mmu/DEVELOPING.md`](mmu/DEVELOPING.md) | TLBs, translation, and page-table walking |
| [`instruction-memory-router.rhdl`](instruction-memory-router.rhdl), [`memory-router.rhdl`](memory-router.rhdl), [`uncached-protocol.rhdl`](uncached-protocol.rhdl) | Physical-region routing and the shared uncached protocol |
| [`cache.rhdl`](cache.rhdl) | Shared cache geometry and replacement helpers |
| [`chi/DEVELOPING.md`](chi/DEVELOPING.md) | Physical-region/Home policy, RN identity, cache transaction engines, and the shared uncached RN-I implementation |
| [`icache/DEVELOPING.md`](icache/DEVELOPING.md), [`dcache/DEVELOPING.md`](dcache/DEVELOPING.md) | Private cache implementation and validation |
| [`tests/`](tests/) | Decode, configuration, public specialization, and invalid-use checks |

## Change the core

1. Identify the owning boundary before editing: decode, scalar pipeline,
   deferred completion, architectural state, translation, cache, CHI engine,
   or top-level composition.
2. Preserve single-issue ordered scalar commit while tracking every deferred
   register-producing operation through its scoreboard and completion path.
3. Add architectural state and serialization rules before integrating an
   execution unit that depends on them. Keep F/D/Zfh specialization host-side
   so disabled hardware elaborates away.
4. Preserve exact fault ownership and priority across Fetch, MMU, PMA routing,
   caches, Execute, Memory, and WB. Do not collapse speculative flush with
   architectural invalidation.
5. Test cycle-visible behavior in the narrowest CIRCT/Verilator fixture, then
   the composed core. Do not add an elaboration snapshot for every submodule.
   Update [README.md](README.md) when public profiles, ports, ordering, timing,
   or deliberate limits change.

## Generated detailed diagrams

The README diagrams describe architectural intent. Generate an implementation
inventory of the elaborated RV64 core, including child blocks, registers, and
typed interface channels, with:

```sh
mkdir -p /tmp/rv5stage-core-diagram
env PLTCOMPILEDROOTS="$(mktemp -d)" \
  racket -y -S "$PWD" tools/write-rv5stage-core-diagram.rhm \
  /tmp/rv5stage-core-diagram
```

The source is
[`../../examples/rv5stage/core-diagram.rhdl`](../../examples/rv5stage/core-diagram.rhdl).
The JSON targets interactive renderers; the compact DOT view links child
modules by name instead of flattening them.

## Focused validation

Run host checks only when decode, configuration, specialization, or public
elaboration-time validation changes:

```sh
make rv5stage-host-test
```

For datapath, cache, pipeline, and state behavior, run `make rv5stage-test` or
select the narrowest backend fixture. Exercise MEM-stage fault classification
or WFI control flow specifically with:

```sh
FIXTURE=rv5stage-data-fault bash tests/backend/run-circt.sh
FIXTURE=rv5stage-wfi bash tests/backend/run-circt.sh
```

The backend test [`DEVELOPING.md`](../../tests/backend/DEVELOPING.md) owns
fixture modes, tool discovery, and artifacts. SoC integration belongs to
[`../../socs/DEVELOPING.md`](../../socs/DEVELOPING.md), and executable target
coverage belongs to [`../../sims/DEVELOPING.md`](../../sims/DEVELOPING.md).
