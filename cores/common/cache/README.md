<!-- Describes the shared physical L1D and opaque-context CHI cache services. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Shared processor caches

This package owns reusable physical L1I/L1D implementations, fixed-line
cache geometry, IO retention, and refill, copyback, snoop, and nonallocating CHI engines. It contains no
register destinations, vector completion slots, translation state, or retirement
policy. RV5Stage and RV2Wide use both caches.
Contributors should read [DEVELOPING.md](DEVELOPING.md).

## Entry point

```rhdl
import:
  lib("cores/common/cache/config.rhm") open
  lib("cores/common/cache/l1d/cache.rhdl") open
  lib("riscv/isa/xlen.rhm") open

inst data(L1DCache(XLen.X64, CacheConfig(64, 2), ~chi: chi_config,
                  ~context: CompletionContext))
```

Supply the selected `XLen`, a hardware `CompletionContext` type, and a
`RiscvHartCHIConfig` containing the physical Home map. The occurrence's RN-F
identity is an input. `CacheConfig` allows 2–64 power-of-two sets, positive
associativity, and fixed 64-byte lines; each way fits within one 4-KiB page.
The XLEN and atomic/locality types reuse existing RISC-V physical-operation
vocabulary; no named core is a dependency.

## Public contract

The current implementation is write-back, write-allocate, set-associative,
with invalid-first tree-PLRU replacement. One miss owns its set while ordinary
loads may hit in independent sets. Same-line authorized requests may wait in
the service queue; its positive `~service_queue_depth` defaults to two and
does not create additional miss engines. Four committed store entries drain
in order. CHI snoops and accepted transactions progress independently of core
redirects.

```text
early lookup -> synchronous arrays -> physical-tag decision -> store candidate
                                            |                      |
                                      load hit/replay       authorization
                                                                   |
authorized requests -> service queue -> lookup/refill/mutation -> response
                              opaque context ---------------------> context
```

`pipeline_lookup` launches an early read; `pipeline.request` supplies the
matching physical address and controls in the following cycle. Load results
are returned in that cycle without an extra cache output register. A store
hit retains a candidate for one cycle: only `commit & commit_ready` authorizes
it. Rejected candidates must be replayed, not retained by the caller.
No speculative lookup allocates, mutates data, or starts a device transaction.

`PhysicalMemoryReq(xlen, Context)` and `PhysicalMemoryResp(xlen, Context)` carry exactly
one opaque `context` field. The cache preserves it through queues, retries,
refills, mutations, and response backpressure. It never interprets it. Every
authorized demand, including a store with no register destination, receives
one ordered response. Prefetches carry no response ownership.

Every accepted authorized request requires a same-edge `virtual_lookup` event
with matching page-offset bits. Physical-only callers supply the physical
address on both paths. This event is not a second transaction or completion;
queued requests retain their own SRAM-read ownership.

At the MMU-to-router boundary, `PhysicalDataReq(xlen, Context, address_width)`
pairs a native-word `memory` transaction with its resolved `pbmt`. RV5Stage
uses an XLEN physical port and superscalar uses a 64-bit physical port for
both XLENs. Each core supplies its own return context and fault protocol.

By default the L1D port and per-way row are one XLEN word. An L1D caller can
specialize `~beat_bytes: 16` and pass 16 as the final argument to `CacheAccess`
and `CachePipelineAccess` for a 128-bit coherent port. Its request payloads
are `PhysicalMemoryBeatReq(xlen, Context, 16)` and `CachePipelineBeatReq(xlen, 16)`.
This changes neither
the 64-byte line nor the CHI flit size. The wide array reads one 16-byte row
per way in parallel; writes select one way and use 16 byte enables.

Scalar requests supply an unshifted value in the low XLEN bits and an explicit
byte mask positioned within the containing beat. Scalar loads return the
width-selected, sign/zero-extended XLEN value in the low bits (upper bits
zero). On the wide port, ordinary loads and stores may instead assert
`raw_beat` at a 16-byte-aligned address: loads return the entire row, and stores
take lane-positioned data and masks without a second shift. This is one cache
transaction, not two independently addressed accesses. Other operations use
scalar mode; no uncached port changes width.
LR/SC, atomics, maintenance, and locality retain the existing physical cache
semantics. `drained` includes committed stores and every accepted demand stage.
Maintenance may return an access error; ordinary accepted requests require
the integrating memory system's successful-completion guarantee.

## Instruction snapshots

The [shared L1I](l1i/README.md) separately returns aligned 32-bit or 64-bit
instruction blocks. It retains ownership-free snapshots through coherent
`ReadOnce` or immutable-ROM `ReadNoSnp` in `CacheLineRead`. Fetch width is
explicit and independent of XLEN; translation, assembly, and architectural
fault metadata stay with the caller.

## Physical classification

[`access.rhdl`](access.rhdl) provides combinational `cache_access_region` and
`cache_access_permitted` helpers for integration boundaries. Footprint lookup
covers the natural access width or entire aligned CBO block, including address
overflow. Callers supply read/write intent, atomic-service eligibility, and
alignment eligibility. `allows_data_cache_allocation` checks the resolved
PMA/PBMT attributes for cacheability, relaxed ordering, and read idempotence.
It applies to ordinary loads and stores at both lookup and authorized service.
`committed_cache_route` selects cached or uncached service for ordinary accesses.
Maintenance uses physical cacheability to choose cached service or local
completion. Callers check permission before admitting the request.

`cache_fragment_permitted` checks the entire aligned physical word of a split
access against the resolved memory attributes and its read or write permission.
An execute-read fragment also requires execute permission. Each core translates
and checks each fragment before issuing it. Both cores use `pte_read_permitted`
for walker reads. All eight PTE bytes must lie in the same readable, cacheable,
read-idempotent region with relaxed ordering under the effective PBMT.

[`cache_prefetch_physical`](prefetch.rhdl) classifies a complete aligned cache
line from an address, prefetch operation, and PBMT. It returns the aligned
address, operation permission, instruction/data cacheability, and the stronger
ordinary-idempotent-memory predicates. Superscalar Bare requires ordinary
data-cacheable RAM even for an instruction hint. Callers choose eligibility,
translation arbitration, staging, and cancellation; the helper adds no state.

## Non-cacheable service

Cached and uncached services share the physical payload and opaque context.
`CacheAccess` adds lookup-independent reservation status; `UncachedMemoryAccess`
wraps each request with its resolved `device` attribute and has no reservation.
The fixed-cycle lookup and store-authorization interfaces remain cache-specific.

`IOMSHR(xlen, Context)` retains one permitted load, store, or block-zero request.
It accepts independently of downstream availability, presents the captured
request the following cycle, and releases only on final completion. It has no
same-cycle refill or extra response buffer. Unsupported operations fault before
allocation. The IOMSHR alone retains the opaque caller context. Its downstream
`UncachedMemoryAccess(xlen, Bool)` sends a constant placeholder; only response
data and fault status come back from the transport.

`CHIUncachedMemory(xlen, config, Context)` performs one nonretrying,
nonallocating transaction at a time. RAM accesses remain coherent (`ReadOnce`
or `WriteUniquePtl`) even when PBMT prohibits caching; device/ROM accesses use
the corresponding NoSnp operation. Block zero serializes eight eight-byte writes
with one completion. The caller's `cancel` input can withdraw only a read still
waiting to issue its CHI request. Once accepted by CHI, it must drain normally;
the caller is responsible for consuming any orphan response. Stores cannot be
canceled. `drained` reports the transport's own idle state, not requester policy.

`UncachedFetchArbiter(xlen, Context)` in `uncached-arbiter.rhdl` shares one
ordered uncached transport between data and instruction requests. It gives
data priority, retains the accepted response owner across flush, and drains
detached instruction replies. It adds no pipeline stage. Connect its `cancel`
to the transport: cancellation can withdraw an unissued read, never erase an
accepted CHI transaction. Callers supply already-shaped, permission-checked
requests and decide which frontend recovery events assert `flush`.

The separate retryable `CacheWriteUnique` engine retains opaque context through
retry/credit handling, DBID acquisition, write-data transfer, and completion.

`UncachedInstructionMemory(xlen, config, Context, fetch_bits)` in
[`uncached-instruction.rhdl`](uncached-instruction.rhdl) composes that data-first
arbiter with `CHIUncachedMemory`. Its instruction port uses the
[physical fetch protocol](../fetch/README.md); `fetch_bits` is 32 or 64 and must
fit the transport word. It shapes aligned instruction reads and drains detached
replies without adding a pipeline stage. The data context remains opaque.

Both cores use [`attach_ordered_memory`](ordered-service.rhdl) to connect the physical
cache port, an IOMSHR, and the core response sink. It returns cached, uncached,
and local-management request sinks, ordering status, and IO admission faults.
Optional Flow transforms convert native cache and IO replies before the shared
`OrderedMemoryService` arbitrates them. Uncached work waits for older cache work and
cached work waits for committed IO and local management completion. Local
management uses one response slot after drain, without inventing a device
transaction. The response arbiter merges local, cached, and IO completions.
`ordered_busy`, `hints_ready`, and `background_ready` expose the same ordering
for speculative cache work, prefetches, and uncached instruction admission.
PMA permissions, PBMT interpretation, byte-lane conversion, and fault reporting
remain with the caller. There is no cancellation port for committed service.

## Integration and limits

Callers check physical permissions, complete-access mapping, cacheability,
alignment, and translation before cache admission. Uncached/MMIO routing and
precise architectural fault ownership belong outside this package. The cache
is not an arbitrary late-faulting memory interface.

[RV5Stage](../../rv5stage/README.md) directly specializes these services with its
architectural destination and core/PTW origin context. [RV2Wide](../../rv2wide/README.md)
owns an ordered completion FIFO and requests raw beats. Frontends, MMUs, and named
core CHI endpoint composition remain outside this package. The shared arbiter
implements owner retention and cancellation; the integrating core chooses flush
policy and connects classified data paths through the shared ordering component.
The [detailed L1D contract](l1d/README.md) describes SRAM scheduling, reservation
protection, maintenance, and coherence behavior.
