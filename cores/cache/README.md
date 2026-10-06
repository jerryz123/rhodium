<!-- Describes the shared physical L1D and opaque-context CHI cache services. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Shared processor caches

This package owns the physical L1D used by RV5Stage and RV2Wide, fixed-line
cache geometry, IO retention, and refill, copyback, snoop, and nonallocating CHI engines. It contains no
register destinations, vector completion slots, translation state, or retirement
policy. Contributors should read [DEVELOPING.md](DEVELOPING.md).

## Entry point

```rhdl
import:
  lib("cores/cache/config.rhm") open
  lib("cores/cache/l1d/cache.rhdl") open
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

Requests carry a byte address, access width, unshifted store value, and an
explicit byte mask positioned within an XLEN beat. Loads return width-selected,
sign/zero-extended values. A caller needing raw beats may use an aligned
full-width request; this also accepts already-positioned masked store data.
LR/SC, atomics, maintenance, and locality retain the existing physical cache
semantics. `drained` includes committed stores and every accepted demand stage.
Maintenance may return an access error; ordinary accepted requests require
the integrating memory system's successful-completion guarantee.

## Non-cacheable service

Cached and uncached services share the physical payload and opaque context.
`CacheAccess` adds lookup-independent reservation status; `UncachedMemoryAccess`
wraps each request with its resolved `device` attribute and has no reservation.
The fixed-cycle lookup and store-authorization interfaces remain cache-specific.

`IOMSHR(xlen, Context)` retains one permitted load, store, or block-zero request.
It accepts independently of downstream availability, presents the captured
request the following cycle, and releases only on final completion. It has no
same-cycle refill or extra response buffer. Unsupported operations fault before
allocation. Context is preserved without inspecting its contents.

`CHIUncachedMemory(xlen, config, Context)` performs one nonretrying,
nonallocating transaction at a time. RAM accesses remain coherent (`ReadOnce`
or `WriteUniquePtl`) even when PBMT prohibits caching; device/ROM accesses use
the corresponding NoSnp operation. Block zero serializes eight eight-byte writes
with one completion. The caller's `cancel` input can withdraw only a read still
waiting to issue its CHI request. Once accepted by CHI, it must drain normally;
the caller is responsible for consuming any orphan response. Stores cannot be
canceled. `drained` reports the transport's own idle state, not requester policy.

The separate retryable `CacheWriteUnique` engine retains opaque context through
retry/credit handling, DBID acquisition, write-data transfer, and completion.

## Integration and limits

Callers check physical permissions, complete-access mapping, cacheability,
alignment, and translation before cache admission. Uncached/MMIO routing and
precise architectural fault ownership belong outside this package. The cache
is not an arbitrary late-faulting memory interface.

[RV5Stage](../rv5stage/README.md) directly specializes these services with its
architectural destination and core/PTW origin context. [RV2Wide](../rv2wide/README.md)
owns an ordered completion FIFO and requests raw beats. L1I, MMUs, and named
core CHI endpoint composition remain outside this package. Fetch cancellation,
instruction/data arbitration, and ordering between cached and IO traffic belong
to the integrating core, not the shared transport.
The [detailed L1D contract](l1d/README.md) describes SRAM scheduling, reservation
protection, maintenance, and coherence behavior.
