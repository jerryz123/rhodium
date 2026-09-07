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
| [`udb.rhm`](udb.rhm) | Exact-version UDB extension closure and fixed RV5Stage architectural parameter claims |
| [`rv5stage.rhdl`](rv5stage.rhdl) | Core, MMU, prefetch routing, cache, uncached, and CHI composition |
| [`core.rhdl`](core.rhdl) | Scalar pipeline, forwarding, hazards, commit, and deferred completion |
| [`bundles.rhdl`](bundles.rhdl) | Scalar pipeline payloads |
| [`../cache-prefetch.rhdl`](../cache-prefetch.rhdl) | Reusable best-effort prefetch operation and request types |
| [`fetch.rhdl`](fetch.rhdl) | Aligned-word window, configured compressed-profile expansion, instruction queue, and redirect flushing |
| [`decode/DEVELOPING.md`](decode/DEVELOPING.md) | Structured integer and FP control generation |
| [`register-file.rhdl`](register-file.rhdl) | Two-read, two-write integer register bank |
| [`fp/DEVELOPING.md`](fp/DEVELOPING.md) | FP payloads, register state, execution lanes, LSU bridges, and completion |
| [`csr.rhdl`](csr.rhdl), [`interrupt.rhdl`](interrupt.rhdl) | Privileged state, traps, counters, and interrupts |
| [`mmu/DEVELOPING.md`](mmu/DEVELOPING.md) | TLBs, demand translation, best-effort prefetch probes, and page-table walking |
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
   WB is the authorization boundary for all instruction side effects: memory
   requests (including potentially side-effecting reads), FP compute, prefetch
   hints, and destination reservations must not issue from EX or MEM. Rejection
   replays before acceptance; accepted operations must never be replayed. Keep
   operand reads and autonomous coherence service independent of authorization.
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

## Maintain the UDB projection

Keep selectable extension membership derived from `RVCoreProfile`. Keep fixed
CSR, trap, alignment, counter, PMP, and LR/SC facts in `udb.rhm`, and pass
physical address width and PMA granularity from the integration boundary. When
one of those behaviors changes, update its RTL owner and UDB claim together.

Run the pure UDB encoder and RV5Stage projection tests, generate a concrete
configuration, and validate it with the UDB version pinned by the ACT4 checkout:

```sh
tools/run-racket-tests.sh riscv/tests/udb-test.rhm cores/rv5stage/tests/udb-test.rhm
make riscv-udb-config RISCV_UDB_CONFIGURATION=simple-soc
bundle exec --gemfile riscv/riscv-arch-test/framework/src/act/data/Gemfile \
  udb validate cfg /tmp/rhodium-udb/simple-soc.yaml
```

UDB semantic validation is required before changing an extension version or
parameter set because schema validation alone does not detect missing
extension-dependent parameters. Keep generated YAML out of version control.

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

For WB authorization and scalar/FP integration, run:

```sh
FIXTURES='rv5stage-core rv5stage-core-rv32f rv5stage-core-rv64d rv5stage-data-fault rv5stage-zicboz rv5stage-interrupt rv5stage-wfi' \
  bash tests/backend/run-circt.sh --simulate-only
```

The RV32F/RV64D benches exercise rejected memory dispatch, committed prefetches,
deferred FP and atomic completion, FP stores/loads, CSR flags, and suppression of younger
FP/register/memory effects behind a data fault. Keep these behavioral checks
at the core boundary rather than depending on generated internal signal names.

For Zicboz, keep permission/fault ownership above the cache, and exercise
both XLEN SRAM sequences as well as the one-completion uncached sequence:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/zicboz-test.rhm
FIXTURES='rv5stage-zicboz rv5stage-csr rv5stage-memory-router rv5stage-mmu-replay rv5stage-dcache rv5stage-dcache-rv32 rv5stage-uncached' \
  bash tests/backend/run-circt.sh --simulate-only
```

The scalar fixture covers request rejection/replay, fence drain ordering,
store-class faults with original `rs1` trap values, and CBZE denial. The cache
fixtures cover all byte offsets and complete-line visibility. The FESVR
payload and simulator validation remain owned by
[`sims/DEVELOPING.md`](../../sims/DEVELOPING.md).

Run host checks only when decode, configuration, specialization, or public
elaboration-time validation changes:

```sh
make rv5stage-host-test
```

For datapath, cache, pipeline, and state behavior, run `make rv5stage-test` or
select the narrowest backend fixture. Exercise WB-stage fault classification
or WFI control flow specifically with:

```sh
FIXTURE=rv5stage-data-fault bash tests/backend/run-circt.sh
FIXTURE=rv5stage-wfi bash tests/backend/run-circt.sh
```

The backend test [`DEVELOPING.md`](../../tests/backend/DEVELOPING.md) owns
fixture modes, tool discovery, and artifacts. SoC integration belongs to
[`../../socs/DEVELOPING.md`](../../socs/DEVELOPING.md), and executable target
coverage belongs to [`../../sims/DEVELOPING.md`](../../sims/DEVELOPING.md).
