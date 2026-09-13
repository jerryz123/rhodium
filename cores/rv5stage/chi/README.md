<!-- Defines the public configuration and transaction contracts of RV5Stage CHI endpoints. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV5Stage CHI endpoints

This package adapts RV5Stage's private caches and shared uncached path to CHI.
It owns the physical-region and Home mapping, placement-specific RN identities,
retry-aware cache transactions, snoop responses, dirty-line writeback, and the
one-outstanding RN-I implementation.

## Configuration and identity

`RV5StageCHIConfig` combines one `CHIFlitParams` value with a nonempty list of
physical regions and their Homes. It derives both the RISC-V physical-memory
map and the CHI Home map from that list. Cacheable regions require HN-F Homes;
uncached regions require HN-I Homes. Executable regions must permit idempotent
reads, atomic regions must be readable and writable, and cacheable regions must
contain complete 64-byte cache lines.
Instruction-only cacheable ROM remains data-uncacheable and uses HN-I;
its region must also contain complete lines. Its PMA contract is defined in
the [RISC-V adapter](../../../riscv/rtl/README.md).
Coherent regions retain instruction cacheability because this core's uncached
instruction path targets HN-I rather than coherent Homes.

`RV5StageCHIParams` describes host-side instruction RN-I, data RN-F, and
optional uncached RN-I nodes. It checks NodeID widths and requires every RN and
Home NodeID to be distinct. `RV5StageCHIIdentity` carries the placement-specific
NodeIDs into hardware so one specialized core can be instantiated at multiple
locations.

## Instruction snapshot read

`RV5StageLineRead` issues retryable 64-byte `ReadOnce` requests through RN-I
channels. It obtains coherent data but no snoopable ownership or dirty
responsibility. It retains context and collects the complete packet set before
acknowledging Home and exposing line data plus an access-fault flag. A consumer
flush cannot abandon an accepted transaction. L1I decides whether a completed
snapshot may install or respond; the engine only owns CHI lifetime.
For instruction-only cacheable ROM, the same engine issues a 64-byte
`ReadNoSnp` with nonallocating, noncacheable CHI attributes and no retry request
or `CompAck`. Completion follows the complete packet set. The read mode and
Home are retained from command acceptance; errors never install a line.
`instruction_home_port(~coherent: ...)` projects the instruction endpoint's
capabilities separately for coherent RAM and noncoherent ROM Homes.

## Cache-line refill

`RV5StageLineRefill` issues retryable `ReadClean` or `ReadUnique` requests,
retains the selected Home and command context, accepts each `CompData` packet
exactly once, and emits `CompAck`. It publishes a completed 64-byte line only
after every packet has arrived and the acknowledgement has been accepted.

## Writes and dirty writeback

`RV5StageWriteUnique` performs one retryable `WriteUniquePtl`, retaining its
address, data, byte mask, Home, DBID, and completion state until the transaction
finishes. `RV5StageLineWriteback` instead issues one retryable, aligned
64-byte `WriteBackFull`. After `CompDBIDResp`, it sends the line as four,
two, or one `CopyBackWriteData` packets on 128-, 256-, or 512-bit DAT.
There is no second completion response or CompAck.

The cache retains the victim as snoop-visible until handoff. Pending snoops
finish before the grant is accepted; the engine then captures the latest victim
state. If a snoop already invalidated it, all copyback packets report Invalid
with zero data and byte enables. Otherwise they carry the complete captured
line and consistent state. Once granted, same-line snoops wait until every
packet transfers. Command context and data remain retained through completion
backpressure.

## Snoop handling

The data RN-F also advertises `CleanShared`, `CleanInvalid`, and `MakeInvalid`.
L1D composes the shared [`CHICacheMaintenance`](../../../chi/transactions/cache-maintenance.rhdl)
engine with SnoopMe enabled, so its own copy is handled by the same snoop path
as peer copies. Maintenance does not hold the cache's SRAM while waiting on
CHI; Home completion, including its error status, is returned to the core.

The data snoop engine exposes `completed`, a one-cycle event on acceptance of
its final response packet. The cache can use it for bounded local/probe
arbitration without reconstructing packet accounting. Backpressure through
`cache_ready` applies to new snoops, not an already accepted transaction.

The clean and data snoop engines retain each accepted request until its cache
lookup and CHI response finish. They pair two-part DVM operations, hold response
traffic stable under backpressure, and return explicit cache updates. Clean
forwarding or `RetToSrc` requests invalidate the local line and report Invalid
so Home can source data elsewhere. A dirty data-cache hit returns the complete
line through `SnpRespData` before invalidating it, except for discard snoops:
`SnpMakeInvalid` and `SnpMakeInvalidStash` discard dirty data and return
`SnpResp_I` without a data transfer. Data-preserving ownership transfers must
use an appropriate snoop such as `SnpUnique` or `SnpCleanInvalid`.

## Uncached access

`RV5StageUncached` arbitrates physical instruction and data requests onto one
RN-I endpoint with at most one outstanding transaction. It emits `ReadNoSnp`
and `WriteNoSnpPtl`, routes read data back to the accepted owner, drains flushed
instruction work without publishing it, reports unsupported data operations as
access faults, and asserts address, Home, response, and packet invariants.
Device writes use the Home's DBID and return write data to that Home; they do
not request direct write transfer.

In the composed core, data requests come from the
[data IO-MSHR](../dcache/README.md#non-cacheable-data-io-mshr), which accepts
independently while the engine serves an instruction. The engine still accepts
only when idle and prioritizes a presented data request over a new fetch. Its
`drained` describes the shared engine, not data-side quiescence; the IO-MSHR
owns the latter from data admission through completion.

For a PMA-authorized `CacheBlockZero`, the engine aligns the address to 64
bytes and serializes eight zero-valued, full-mask 64-bit writes. Only the
last acknowledged write produces a core response. No other request can
interleave, and `drained` stays false until the entire operation completes.
The engine does not recheck PMAs; the upstream router owns block-wide permission.

See [`DEVELOPING.md`](DEVELOPING.md) for source ownership, dependency rules,
and focused validation.
