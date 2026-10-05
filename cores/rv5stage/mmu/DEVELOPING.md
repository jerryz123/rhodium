<!-- Guides contributors through implementing and validating RV5Stage translation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the RV5Stage MMU

Read the MMU [README](README.md) for request flow, TLB and walker contracts,
fault ownership, Sv39 behavior, and deliberate limits. This guide owns source
placement, change workflow, and focused validation.

## Architecture and ownership

The MMU sits between virtual core requests and the physical memory hierarchy.
It owns TLB lookup/refill, serialized walking, fault correlation, fixed-latency
fetch outcomes, misaligned ordinary-access preflight and fragmentation, and
separate translated-core and walker physical requests. The parent core owns CSR
sequencing, trap priority, atomic/LRSC alignment, and final exception causes;
the physical router and cache own admitted transaction behavior.

Reuse the public RISC-V Sv39 adapter for PTE layout, canonicality, permissions,
superpages, and physical-address construction. Keep translation state in the
MMU and core-first physical arbitration in the parent
[`data-port-arbiter.rhdl`](../data-port-arbiter.rhdl), not in the pure RISC-V
model or caches. The shared walker retains an accepted PTE through cancellation
and drains its orphan reply before admitting another walk. The MMU forwards all
accepted replies, even after invalidation, and tracks the physical pending read.
This is response correlation, not whole-port ownership.

## Implementation map

| File | Ownership |
|---|---|
| [`protocol.rhdl`](protocol.rhdl) | Translation request/result bundles, fetch-fault metadata, and walker memory interface |
| [`tlb.rhdl`](tlb.rhdl) | One host/guest entry bank, combinational demand/probe matching, separate stage permissions, refill, and host-port adapter |
| [`walker.rhdl`](walker.rhdl) | One host/nested walk FSM, saved VS continuation, PTE checks, cancellation/drain, and host-port adapter |
| [`translation.rhdl`](translation.rhdl) | Shared host/guest lookup, mapping, fill, PTE-memory contracts, and host-port value projections |
| [`../tests/translation-service.rhdl`](../tests/translation-service.rhdl) | Test-only serialized command driver for the shared TLB/walker |
| [`vector-window.rhdl`](vector-window.rhdl) | Two-page macro-owned translation authorization and full-page ordinary-memory certification |
| [`mmu.rhdl`](mmu.rhdl) | ITLB/DTLB composition, miss priority, exact-request instruction fault-outcome retention, replay-owner walk admission, fault correlation, registered fetch outcomes, registered virtual/physical prefetch stages and cancellation, misaligned access preflight, physical checks, and separate core/PTE physical offers |
| [`misaligned-access.rhdl`](misaligned-access.rhdl) | One- or two-word physical fragment sequencing, original-owner retention, load assembly, and single-completion return after MMU preflight |
| [`../data-port-arbiter.rhdl`](../data-port-arbiter.rhdl) | Core-first physical request and lookup selection, fault demultiplexing, and origin-tagged response routing |
| [`../rv5stage.rhdl`](../rv5stage.rhdl) | Core, L1I, physical-router, and privileged-control integration |
| [`../../../riscv/rtl/sv39.rhdl`](../../../riscv/rtl/sv39.rhdl) | Shared Sv39 decoding, canonicality, permission, superpage, and address helpers |
| [`../tests/mmu-test.rhm`](../tests/mmu-test.rhm) | Public translation types, widths, and composition boundary |
| [`../tests/mmu-data-port-fixture.rhdl`](../tests/mmu-data-port-fixture.rhdl) | MMU plus production data-port arbiter used by the cycle-level replay fixture |
| [`../../../cores/rv5stage/tests/circt/verilog/rv5stage-mmu-replay_tb.sv`](../../../cores/rv5stage/tests/circt/verilog/rv5stage-mmu-replay_tb.sv) | Cycle-level pulsed DTLB miss, three-level walk, translated replay, and prefetch latency, throughput, rejection, and cancellation |

## Change translation behavior

Effective explicit-access privilege comes from the shared
`riscv/rtl/privilege.rhdl` helper, also used by the core's pointer-mask policy.
Keep this selection consistent across data translation and prefetch probes.
Pointer normalization itself is upstream in EX, not a second MMU transform.
PMM writes restart fetch and therefore clear pending prefetch stages, but do not
invalidate translations or cancel accepted page-table response ownership.

1. Decide whether the change is reusable Sv39 representation/policy or
   RV5Stage state and arbitration. Put only the former in `riscv/rtl`.
2. Preserve address correlation for walk completions and faults. The instruction
   attempt is captured into S1 after S0 admission; the frontend may change its
   payload immediately. An unresolved attempt produces S2 replay, never a
   retained request or local reread. A request transferred with instruction
   flush replaces the old S1 context; a flush without transfer clears it. Gate
   S1 walk initiation and physical resolution with the frontend's
   younger-attempt kill.
3. Keep page faults distinct from physical PTE access faults and suppress every
   rejected physical resolution before it reaches a cache or device. Early
   virtual SRAM reads are permitted, but cannot create a successful cache token
   without the paired physical request. Wire lookup paths directly in the parent
   composition so translation/PMA cannot feed their indices or validity.
   The ordinary `pipeline` path registers EX load/store context before sharing
   the demand DTLB in MEM. WB requests win contention. Distinguish contention
   replay from miss/uncached slow service and precise faults. Do not gate this
   path on `core_memory.drained`: buffered stores are resolved by physical-byte
   checks in L1D. `ordered_busy` separately blocks younger work behind IO.
   Forward WB authorization and readiness unchanged; WB owns squash and
   serialization. Do not reconnect drain-derived flush to store authorization.
   Misaligned ordinary accesses are rejected by the speculative MEM lookup and
   enter the WB slow owner. Drain older memory work before capturing one; for a
   cross-word access, resolve both virtual pages and both physical word regions
   before issuing either fragment. Retain the original writeback owner through
   one response, and leave atomic/LRSC alignment traps in the scalar core.
   Keep `request_fault_address` virtual and paired with the selected fragment's
   guest provenance. Scalar/vector arbitration gates the owning fault flags;
   only that owner may consume the shared address sideband at retirement.
   Do not add virtual trap metadata to the physical cache protocol.
   Never use relaxed prefetch A/D permissions or start a speculative data walk.
   Fork lookup context explicitly between early virtual indexing, translation,
   and requester ownership. Use filtered/mapped physical-request flows; route
   cache-return ancestry only when the cache supplied the selected outcome.
   Local faults, contention, and absent-response fallbacks retain translation
   context instead. Do not infer response ownership from matching addresses.
4. Give an offered translated core WB demand priority over an offered PTE read.
   Do not reserve the port for a future MEM-to-WB handoff or an accepted PTE
   response. Select the matching early virtual/physical L1D lookup with each
   physical request, independently of downstream readiness. Route immediate
   admission faults to the granted requester and delayed completions by the
   explicit `RV5StageDataOrigin`, not by writeback kind or address. The shared walker's
   drain state prevents a canceled reply from satisfying a later walk.
   Ordinary fetch recovery detaches the instruction consumer without resetting
   the walker or an accepted PTE request. Retain successful ITLB fills and
   exact-request instruction page-fault outcomes, but suppress architectural
   fault capture for the detached consumer, including a flush on the completion
   edge. Do not cache PTE access faults or expose a fault outcome as a successful
   translation or prefetch probe. Invalidate retained outcomes with the ITLB.
   A core replay owner may refetch both words of its own instruction; defer
   younger instruction walks until that owner commits or traps. Keep this distinct
   from architectural invalidation and from clearing an already-latched fault.
5. Recheck current privilege, `SUM`, `MXR`, `A`, and `D` on every TLB hit; do
   not cache a prior permission decision.
   The explicit exception is a certified vector macro: `vector-window.rhdl`
   retains its two translations and authorization under a stable architectural
   context until execution releases them. CSR/privilege changes, SFENCE, traps,
   and interrupts cannot pass this owner. DTLB replacement can proceed normally.
   Vector issue releases the window after its final non-replayable acceptance:
   successful slow requests already crossed the combinational physical-address
   mapping, and accepted lookup hits no longer consult translation. Delayed
   responses retain slot ownership but do not retain or reuse the page window.
   A subsequent macro can therefore certify while older responses drain.
   The `pipeline_vector` owner bit travels with the registered lookup; slow
   requests identify vector ownership through their existing writeback union.
   Never apply a window to a scalar request or recheck its pages through the
   replaceable DTLB. Assert that every authorized vector request is inside it.
   The speculative one-page MEM check shares the demand DTLB read and its
   normal access-permission evaluation, but cannot displace a committed demand,
   existing precheck, or ordinary pipeline lookup. It never walks or touches
   L1D. Capture its physical page and context in MEM/WB; WB validates and carries
   them with an accepted descriptor without requiring an idle vector window.
   Each certified request carries its physical address, derived from the
   captured page, and bypasses both the DTLB and the fallback page window.
   The vector-physical sidebands are gated by the selected lookup/request
   owner; they must never bypass translation for scalar traffic. Check context
   equality and the invalidation epoch at WB. A mismatch before WB rejects the speculative
   certificate; admitted vector ownership keeps the context stable thereafter.
   Certificates include virtualization and the complete shared translation
   context: both modes/root PPNs, effective privilege, SUM/MXR and PBMTE controls.
   A host SATP tag alone cannot authorize a guest mapping. Construct the context
   for normal data access, independently of concurrent scalar explicit-guest
   intent. Guest entries are composed 4 KiB mappings, so a two-page window must
   establish both pages. A failed precheck only selects elementwise execution;
   that exact element owns any architectural guest fault and GPA/PTE provenance.
   DTLB replacement alone does not invalidate the carried translation.
   Page probes reuse the demand DTLB and serialized walker, including ordinary
   A/D checks; unsuccessful probes reply false rather than populating the
   architectural fault latch. They never issue data accesses. A matching
   superpage supplies both adjacent page translations from one result.
   Offer the first page's DTLB lookup on precheck admission. Return a hit
   combinationally to the vector window so it can capture the authorization on
   that edge; if a committed demand owns the DTLB, retain the span and retry.
   A miss uses the unchanged generic walker request, with only the MMU wrapper
   recording that its result belongs to the vector probe. Repeat the lookup
   for a second page unless the first hit covers both through a superpage.
   Whole-page PMA checks exclude devices, non-idempotent memory, and subpage
   maps. Failed conservative coverage selects element-wise execution.
6. Keep prefetch probes non-faulting and independent of walker ownership; they
   may use Bare translation or an existing TLB entry but must not check A/D.
   For composed mappings, intersect VS/G permissions for each access class
   before taking the fetch/load/store union. Independent per-stage unions can
   incorrectly authorize disjoint permissions. Apply the G PBMT override,
   followed by any non-PMA stage-one override.
   Keep address, operation, and validity registered on both sides of the probe;
   cancellation is synchronous so demand squash cannot reach cache admission
   through a combinational prefetch-valid gate.
   Use explicit flush ports on both always-capture prefetch pipes so event
   lineage observes cancellation without introducing a separate reset epoch.
7. Keep host checks to public translation contracts. Test walk, cancellation,
   fault, and invalidation behavior in compiled simulations, then update
   [README.md](README.md) for observable changes.

The walker binds `mmu/walk` to its active state (excluding Idle and Drain), request acceptance
gated by cancellation priority, and active completion/cancel release.
One retained contract covers its PTE requests and completion; no new functional
owner state or inferred address matching is needed. Keep ordinary instruction
recovery distinct from `walker.cancel` when extending this instrumentation.

## Focused validation

Select `rv5stage-guest-translation` for the test-only composed translation service. Its public
memory/completion scoreboard checks cold walks, warm hits without PTE traffic,
host/guest coexistence at the same VA, host superpage reach, guest 4 KiB slicing, replacement, current permissions, byte-exact GPA
faults, root/mode/environment changes, captured caller context, physical faults,
held completion, cancellation, and invalidation during accepted reads and
completion. Pair it with `rv5stage-nested-walker` when shared walk/result
contracts change. The precise cause helper consumes the original access class;
its test covers fetch/load/store/cache-management implicit-PTE faults.
Keep this command sequencer under tests; production ITLB/DTLB hits remain combinational.
The production MMU uses the shared ports directly. The optional guest context
comes from the CSR owner; current privilege/MPRV/MPV chooses fetch/data stage
contexts separately. The core retains the paired request/MEM fault metadata,
while S2 captures fetch provenance alongside its response. Keep the physical
cache ABI independent of guest translation. Run `rv5stage-hypervisor-core` for
this end-to-end boundary.
Its split-access cases cover scalar/FP/vector loads and stores, first/second
page faults, warm permission failures, PMA rejection, HS/VS delegation, and
explicit HLV/HSV guest faults. They check trap values, EPC, GPA provenance,
vector restart index, and suppression of the rejected store's partial effects.
Explicit guest requests carry a typed mode in the virtual MMU protocol; cache
protocols remain physical. Register pipeline translation mode alongside its
request, and give the WB transaction's mode the same DTLB priority as its
address. SPVP, not MPRV or live V, chooses explicit guest privilege. Recheck
HLVX execute permission on warm hits at both stages and physical R/X permission
before admission; nested PTE reads retain ordinary read permissions.
TLB invalidation and walk-owner removal happen at the invalidation edge;
the walker's cancel notification is registered to avoid a WB-to-arbiter ready
loop. Reads accepted on that edge remain real transactions and must drain.

For nested translation, select `rv5stage-nested-walker`. Its public-interface
fixture checks all Bare/paged combinations, the 15-read cold nested walk,
Sv39x4 root geometry, mixed superpages, captured context, per-stage privilege,
MXR/SUM/PBMTE and A/D checks, exact guest-fault provenance, physical rejection,
backpressure, zero-latency responses, and cancellation/draining. The saved VS
frame owns the continuation while the active frame runs G translation; no
second walker or nested memory requester is instantiated. Keep memory-response
ownership in the shared walker; the MMU must not filter replies on invalidation.
Shared PTE/address helpers remain in `riscv/rtl`; retained frames and arbitration
remain here. Keep H profile publication separate from component validation.

Mapping geometry belongs in the public Sv39 adapter. Keep walker `level`
separate from result/entry `page_size`, and normalize `base_ppn` at leaf
construction. A 64 KiB NAPOT entry has one permission/A/D snapshot from its
source PTE; do not scan aliases or add hardware A/D updates. Vector certificates
remain 4 KiB physical-page certificates, not mapping-base PPNs.

For Svnapot, run `rv5stage-svnapot`, `rv5stage-mmu-replay`, and
`rv5stage-walk-trace`, plus the profile, MMU, and `rv5stage-test.rhm` composition
host checks. The direct fixture covers every subpage, malformed encodings,
current permissions, PBMT, compact
replacement, held results, and invalidation. The integrated replay fixture
covers scalar/fetch/prefetch reuse and two-page vector authorization both
within and across 64 KiB boundaries. ISA/UDB publication follows the selected
Svnapot/Svpbmt flags; the shared RVA23 preset selects both. Keep component
validation and exact-profile ACT projection distinct from full RVA23
conformance, which enabling these extensions alone does not establish.

For the Svpbmt translation foundation, run `rv5stage-svpbmt`, `rv5stage-csr`,
and `rv5stage-mmu-replay`. PBMTE is captured at walk admission; do not sample
the caller's next request while validating later PTE replies. PBMT travels
with translation results and TLB entries, including the prefetch probe.
The composed MMU resolves demand/fetch/prefetch attributes with the shared
adapter and rejects NC/IO vector certificates, keeping their effects on the
ordinary element path. The physical request wrapper owns PBMT through
arbitration; never recover it from live CSR state at response time. Preserve
PTE-response draining across cancellation. Use `rv5stage-hypervisor-core` for
both-stage and Bare guest execution, implicit-PTE attributes, scalar/vector
fallback and faults; `rv5stage-memory-router`, `rv5stage-io-mshr` and
`rv5stage-uncached` cover routing, retained ordering and physical-domain opcodes.

Run the MMU-owned host check from the repository root:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/mmu-test.rhm
FIXTURE=rv5stage-mmu-replay bash tools/testing/circt/run.sh --simulate-only
FIXTURE=rv5stage-walk-trace bash tools/testing/circt/run.sh --simulate-only
```

The instrumented walker fixture checks exact residency nodes, end cycles, and
PTE/completion edges from public transfers, with repeated addresses, three-level
walks, page/access faults, stalled completion, cancellation in three phases,
cancelled admission, and reset while occupied.

The wrapper selects the persistent worktree-specific root when none is supplied. Keep this
test limited to public translation contracts; do not add internal operation or
state snapshots. The Verilator fixture pulses one data request, checks the three
expected PTE addresses, and requires a later retry to use the filled DTLB while
preserving request metadata. It also checks stalled walker and core requests,
older MEM-to-WB demand priority over an offered PTE, core admission while a
PTE response is pending, and isolation of PTE replies from ordinary replies.
Instruction recovery is exercised during a stalled PTE request,
request acceptance, delayed response, response arrival, and completion. Refetch
must reuse a successful detached fill without additional PTE traffic; detached
faults must neither escape nor block the next walk.
The same fixture certifies split pages with noncontiguous physical mappings,
checks next-cycle responses for warm DTLB hits and held responses, replaces
DTLB entries while a vector window remains live, verifies one-walk superpage
coverage, and rejects a second-page fault
without reporting a trap from the precheck.
It checks prefetch latency and back-to-back
throughput, TLB selection and rejection, Bare/PMA behavior, and synchronous
cancellation at either stage on flush, invalidation, context change, and reset.
Use the parent
[`DEVELOPING.md`](../DEVELOPING.md#focused-validation) when changes span CSR
sequencing, the pipeline, physical routing, or caches.
