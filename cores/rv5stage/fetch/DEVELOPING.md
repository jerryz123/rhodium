<!-- Guides contributors through RV5Stage fetch and branch-prediction source ownership. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RV5Stage fetch

Read this package's [README](README.md) and the parent
[RV5Stage guide](../DEVELOPING.md) for the complete pipeline contract. This
directory owns instruction acquisition from the S0 request through the
packet-to-Decode boundary. The sibling [`icache/`](../icache/) package owns
instruction storage, refill, and coherence rather than fetch sequencing.

## Implementation map

| Area | Ownership |
|---|---|
| [`protocol.rhdl`](protocol.rhdl) | Core/frontend control and the final fetch-to-Decode payload |
| [`frontend.rhdl`](frontend.rhdl) | S1/S2 correlation, repair, reservation, and packet queue topology |
| [`source.rhdl`](source.rhdl) | S0 PC selection, continuation, replay, and prediction lookup |
| [`packet.rhdl`](packet.rhdl), [`scan.rhdl`](scan.rhdl) | Raw packet representation, prediction-cut validation, and S2 control-flow predecode |
| [`instruction-buffer.rhdl`](instruction-buffer.rhdl) | Compressed expansion, straddling assembly, and residual-halfword state |
| [`../../common/bpred/`](../../common/bpred/DEVELOPING.md) | Shared prediction payloads, BTB state, RISC-V hints, and speculative/resolved RAS |

## Dependency direction

The fetch pipeline consumes the shared `cores/common/bpred/` components and protocols,
and [stateless instruction encoding helpers](../../common/fetch/encoding.rhdl).
S2 uses only parcel shape: a 32-bit prefix can identify a possible branch
without supplying its target. Keep the complete-instruction gate on fallback
targets. ID uses the expander for its own ISA catalog (raw passthrough without C);
it retains ownership of head and continuation faults, the residual parcel and Decode backpressure.
Neither path instantiates the wide assembler or issue window.
Keep cursor selection, prediction validation, fallback discovery, and RAS update
qualification here; shared predictors must not import fetch consumers.
Keep `FetchDecode` in `fetch/protocol.rhdl` rather than the scalar
pipeline's general `bundles.rhdl`, so instruction assembly does not depend back
on execution-owned payloads.
Its `fault` field uses shared `FetchFault` through ID/EX. Assembly resolves the
existing access-before-page priority and carries `RiscvGuestFault` directly;
EX reconstructs the fault address from the exact instruction PC and continuation
bit. The queue's boundary bit selects PC expansion independently. Preserve GPA
byte adjustment only for explicit faults, leaving implicit PTE GPAs unchanged.
Cache responses and raw queue flags keep their existing format and timing.

The core may consume fetch and predictor protocols and use the RAS classifier
when resolving an instruction. Tests remain under [`../tests/`](../tests/), and
behavioral drivers and emitters live under [`../tests/rsim/`](../tests/rsim/).
Do not add root-level forwarding modules for old paths; update consumers as one
move so the directory boundary remains visible.

The scanner refreshes validated return hints from the live RAS at S2, before
the admitted packet updates the stack. A changed warm target uses the existing
late-redirect path, not structural-entry invalidation. For an upper-halfword
32-bit return, publish corrected prefix metadata and apply its action once;
retain the pending redirect until the continuation is admitted. Redirecting at
the prefix would kill the continuation, and re-reading the stack afterward
would use the already-popped head. Faults and epoch clears suppress that redirect.

Keep full prediction PCs in scan, repair, and RAS feedback. Encode parcel ownership
on the packet branch only: the scanner knows whether a newly discovered fallback
starts in its retained prefix. On transfer to residual storage, keep only whether
the upper parcel owns the prediction. A live continuation can supply the hint
for that prefix; it cannot override a standalone compressed residual.

The scanner, queue, and instruction buffer use one `RV5StageFetchPacket` with
fixed 39-bit RV64 or 32-bit RV32 PCs. The MMU rejects nonrepresentable instruction
addresses after translation and before physical fetch, including guest-Bare;
existing translation faults take priority. The scanner checks `CompactPc.fits`
before capturing successful PCs. RV64's tagged outcome shares prediction storage
with fault flags and one positive sequential-boundary bit, including H configurations. Every
entry reconstructs its own fault address under Decode backpressure. Consumers
request address, fault flags, or prediction separately, without an expanded clone.

BTB source admission and BTB/RAS targets use the same fixed PC width. Source
queries outside that bound miss rather than alias. The backend rejects arbitrary
nonrepresentable redirects; only sequential crossings need the frontend boundary
encoding. Only speculative destinations may alias.
The cursor, S1, and S2 retain a straddling prediction's destination as a
`CompactPc`, including through replay. Expand it only when selecting it as
the next PC; the fetch attempt's own PC stays XLEN-wide for boundary faults.
The source uses the predictors' normalized hints, and the scanner normalizes direct-jump
and live-return fallbacks before publishing both packet metadata and redirects.
Do not truncate a retained prediction independently of the address fetch followed.
EX still compares against the full architectural next PC; restart, repair PCs,
and fault addresses remain exact. RAS resolved entries are compact predictor
state, not the architectural link registers. The branch-prediction fixture
warms an indirect destination then changes only its discarded high bits, follows
the old valid alias, rejects a wrong-path store, and checks full-width mepc/mtval/cause.

## Focused validation

Scanner packet, repair, and fallback outputs derive from explicit ingress
forks. Keep fallback gating and payload unchanged when mapping its Flow
branch: the existing late-redirect pipe and cursor retain the triggering S2
occurrence. This identifies the fallback's triggering packet, not provenance
of retained predictor/halfword state. No manual event edge or extra checkpoint
is needed. `rv5stage-fetch-prediction` checks exact S2-to-S0 parent occurrences
for direct/compressed jumps, returns, straddles, and blocked redirect issuance
alongside ordinary S0-to-S0 successor ancestry and functional prediction checks.
The `rv5stage-instruction-buffer` and `rv5stage-instruction-buffer-no-c` fixtures
distinguish illegal compressed expansion from a head fault and from a raw
word left for ordinary instruction decode.
The `rv32` and `h` instruction-buffer variants also cover fault-address wrap,
positive canonical boundaries, backpressure, access/page priority, and explicit
versus implicit guest GPA provenance. Keep the same native oracle across variants.

After changing layout or imports, run `moon run rhodium:check-boundaries` and confirm no old
paths remain. Direct predictor behavior is covered by the shared `bpred-btb`
and `bpred-ras` fixtures. Fetch integration is covered by `rv5stage-instruction-buffer`, `rv5stage-fetch-prediction`,
`rv5stage-fetch-throughput`, `rv5stage-return-prediction`,
`rv5stage-branch-prediction`, `rv5stage-fetch`, `rv5stage-core`, and
`event-frontend` rsim fixtures. Run Racket and Rhombus checks through the
repository wrappers so they use the managed worktree cache.
