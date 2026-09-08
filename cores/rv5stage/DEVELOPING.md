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
| [`fetch.rhdl`](fetch.rhdl) | Independent request/assembly PCs, four-word reservation ring, compressed expansion, instruction queue, and redirect flushing |
| [`decode/DEVELOPING.md`](decode/DEVELOPING.md) | Structured integer and FP control generation |
| [`register-file.rhdl`](register-file.rhdl) | Two-read, two-write integer register bank |
| [`fp/DEVELOPING.md`](fp/DEVELOPING.md) | FP payloads, register state, execution lanes, LSU bridges, and completion |
| [`csr.rhdl`](csr.rhdl), [`interrupt.rhdl`](interrupt.rhdl) | Privileged state, traps, counters, and interrupts |
| [`mmu/DEVELOPING.md`](mmu/DEVELOPING.md) | TLBs, demand translation, best-effort prefetch probes, and page-table walking |
| [`instruction-memory-router.rhdl`](instruction-memory-router.rhdl), [`memory-router.rhdl`](memory-router.rhdl), [`uncached-protocol.rhdl`](uncached-protocol.rhdl) | Physical-region routing, data IO-MSHR composition, and the shared uncached protocol |
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
   Keep integer bypass selection in Decode and register it with the captured
   operands. Do not qualify forwarding with live MEM fault/replay/kill results;
   those cancel younger token validity, independently of payload capture.
   Use `ValidPipeAlwaysCapture` for these stage boundaries.
5. Test cycle-visible behavior in the narrowest CIRCT/Verilator fixture, then
   the composed core. Do not add an elaboration snapshot for every submodule.
   Update [README.md](README.md) when public profiles, ports, ordering, timing,
   or deliberate limits change.

Keep frontend stage ownership explicit: Fetch owns aligned request generation
and assembly, MMU owns registered S1 address/translation and local read retry,
and L1I owns S0 SRAM admission and always-captured S2 resolution. Do not fold
same-cycle word consumption into the request address or move ITLB lookup back
onto a live core request. Use the fetch, MMU-replay, I-cache, instruction-router,
and IO-boot fixtures when changing this boundary.

The assembled-instruction buffer uses a five-entry flow-through `ShiftQueue`
with full-queue pipelining disabled. Decode readiness may control its shifts,
but must not reach word-request admission or address generation combinationally.
The four-word `reserved` count includes both outstanding requests and returned
words; return credit only when assembly releases a word at the clock edge.
Do not count in-flight requests a second time or borrow same-cycle dequeue
credit. The `rv5stage-fetch-admission` structural fixture guards this timing
contract using hierarchical port-leaf dependencies; run it in `--verify-only`
mode alongside the behavioral fetch and I-cache fixtures.

Mul/div dispatch validity comes from authorized commit, but operand payloads
come directly from the normal WB pipeline token. Retained CMO and WRS retirement
contexts must not select arithmetic operands. The reusable multiplier captures
raw operands before its magnitude-preparation cycle; keep that register boundary
between WB selection and full-width negation.

## Pipeline event annotations

`core.rhdl` describes the existing stage instances with public interface trace
contracts. Keep storage certification bound to those instances; do not replace
always-capture payload registers or derive controls from generated signal names.
EX's payload is still computed unconditionally; its flow filter qualifies only
token validity, preserving the feed-forward datapath and cancellation timing.

Fetch is an explicit root because cache/MMU/fetch assembly is outside the traced
lineage. Later checkpoints must not become independent roots to hide an
unsupported path. Decode fires only after hazard gating. Keep WB arrival distinct
from architectural retirement and deferred completion.

After edits, run `rv5stage-core` for forwarding, stalls, replay, redirects, and
deferred completion, then the SimpleSoC trace smoke. Its native Perfetto checks
follow exact occurrence edges, extract RV64 PCs from the canonical packed
payloads, and check one-cycle or elastic delays without requiring every fetched
token to survive. The conversion/fanout compiler fixture covers both Valid
replication outputs, including a dropping branch.

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

For NTL WB association and request propagation, select `rv5stage-ntl`,
`rv5stage-mmu-replay`, `rv5stage-memory-router`, and `rv5stage-dcache`.
The core bench checks all four selectors, non-memory consumption, replacement,
integer/FP memory, rejected request replay, branch squash, synchronous traps,
interrupt entry, and absence of an older-load drain. Adapter benches check
locality preservation and walker isolation. The `rv5stage-dcache` and
`rv5stage-dcache-rv32` benches check coherent non-allocating load misses,
resident-line preservation, destination/lane handling, and unchanged default
allocation; the RV64 bench also checks retry, CompAck backpressure, and LR state.
`memory.rhdl` owns the locality vocabulary, independently of decode selectors
and cache policy. Keep the entire request in lookup/transaction context rather
than reconstructing metadata at refill completion.

For PAUSE, run the catalog/overlay/profile tests and `rv5stage-pause`, plus
`rv5stage-wfi` and `rv5stage-zawrs` when changing issue/interrupt gating.
`decode/hint-ctrl.rhdl` owns the nonarchitectural hint selector. Pipeline
payloads carry it to WB, where PAUSE retires before starting a bounded counter.
The hint's younger-issue barrier is distinct from system/fence serialization:
PAUSE must not inherit an older-work drain. Keep interrupt and completion
service outside the cooldown gate.

For WB-owned Zawrs waiting and the cache-owned reservation observation path:

```sh
FIXTURES='rv5stage-zawrs rv5stage-wfi rv5stage-dcache rv5stage-memory-router rv5stage-mmu-replay' \
  bash tests/backend/run-circt.sh --simulate-only
```

The WRS bench checks retirement deltas through CSRs, original trap PC/value,
globally masked and enabled interrupt wake, timeout and privilege policy, and
invalidation before and during entry. Cache and adapter benches cover the
reservation level independently of instruction waiting. Keep pending WRS
context in the core, LR/SC state in L1D, and timer/interrupt policy independent
of physical clock gating. Select Zawrs through `RV5StageExtensions`, keeping
core decoder selection, ISA descriptions, and UDB claims derived from that
same profile. The UDB database names the ratified extension version `1.0.0`.

For Zihpm CSR catalogs, profile claims, and access semantics, run:

```sh
export PLTCOMPILEDROOTS="$(mktemp -d)"
tools/run-racket-tests.sh riscv/tests/csr-test.rhm tests/frontend/riscv-csr-bank-test.rhm cores/rv5stage/tests/profile-test.rhm cores/rv5stage/tests/udb-test.rhm
FIXTURES='rv5stage-zihpm-rv32 rv5stage-zihpm-rv64 rv5stage-csr' \
  bash tests/backend/run-circt.sh --simulate-only
```

The two Zihpm benches share an XLEN-parameterized sweep of every HPM slot,
write-ignore behavior, read-only write intent, RV32 high halves, and S/U
access denial. Keep the CSR-bank grouping in the RISC-V adapter and profile
permission policy in this core; do not add counter state for zero-valued slots.

For data IO-MSHR admission, ordering, and shared RN-I contention, run:

```sh
FIXTURES='rv5stage-memory-router rv5stage-uncached rv5stage-io-mshr rv5stage-io-boot' \
  bash tests/backend/run-circt.sh --simulate-only
```

The router fixture covers RV32 permission rejection and cached/uncached
exclusion. The composed IO-MSHR fixture covers RV64 retained payloads, fetch
arbitration and cancellation, backpressure, exactly-once completion, and reset.
The complete-core boot fixture executes the generated polling ROM with delayed
entry publication, secondary-hart parking, and fence-ordered signature stores
at three CHI response latencies;
neither L1 cache may issue a request. This full-core fixture uses the SoC harness's
Verilator `UNOPTFLAT` warning setting for packed interfaces; assertions and
runtime convergence checks remain enabled. Keep simulator entry programming and SoC
BootROM policy separate from this core-level regression.

For instruction-router flow changes, select `rv5stage-instruction-memory-router`
and `rv5stage-io-boot`. The router bench covers owner-queue capacity, request and
response stalls, cached/uncached response ordering, and flush cancellation.

For Zicbom, use the composed decode test and the WB, MMU, physical-router,
and self-snooped cache fixtures:

```sh
export PLTCOMPILEDROOTS="$(mktemp -d)"
tools/run-racket-tests.sh cores/rv5stage/tests/zicbom-test.rhm cores/rv5stage/tests/rv5stage-test.rhm cores/rv5stage/tests/udb-test.rhm
FIXTURES='rv5stage-zicbom rv5stage-csr rv5stage-mmu-replay rv5stage-memory-router rv5stage-dcache rv5stage-dcache-rv32' \
  bash tests/backend/run-circt.sh --simulate-only
```

Keep retirement context in the core, reusable xenvcfg policy in `riscv/rtl`,
and transaction lifetime in `CHICacheMaintenance`. The data response's
`access_fault` is a completion status, distinct from pre-acceptance request
faults. Ordinary accesses currently produce successful completion status;
do not silently generalize asynchronous ordinary-load error retirement.
The pending CMO must never reissue, accept younger instructions, or block the
cache's independent snoop service.

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
