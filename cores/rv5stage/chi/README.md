<!-- Defines the public configuration and transaction contracts of RV5Stage CHI endpoints. -->

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

`RV5StageCHIParams` describes host-side instruction RN-F, data RN-F, and
optional uncached RN-I nodes. It checks NodeID widths and requires every RN and
Home NodeID to be distinct. `RV5StageCHIIdentity` carries the placement-specific
NodeIDs into hardware so one specialized core can be instantiated at multiple
locations.

## Cache-line refill

`RV5StageLineRefill` issues retryable `ReadClean` or `ReadUnique` requests,
retains the selected Home and command context, accepts each `CompData` packet
exactly once, and emits `CompAck`. It publishes a completed 64-byte line only
after every packet has arrived and the acknowledgement has been accepted.

## Writes and dirty writeback

`RV5StageWriteUnique` performs one retryable `WriteUniquePtl`, retaining its
address, data, byte mask, Home, DBID, and completion state until the transaction
finishes. `RV5StageLineWriteback` serializes one dirty 64-byte line into eight
such 64-bit writes and completes only after the final beat completes.

## Snoop handling

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

For a PMA-authorized `CacheBlockZero`, the engine aligns the address to 64
bytes and serializes eight zero-valued, full-mask 64-bit writes. Only the
last acknowledged write produces a core response. No other request can
interleave, and `drained` stays false until the entire operation completes.
The engine does not recheck PMAs; the upstream router owns block-wide permission.

See [`DEVELOPING.md`](DEVELOPING.md) for source ownership, dependency rules,
and focused validation.
