<!-- Documents RV3Wide's three-lane specialization and shared execution services on a 4R3W pipeline. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV3Wide

RV3Wide is an experimental named entry point to the
[shared superscalar pipeline](../superscalar/README.md). It can issue and retire
three instructions per cycle; all three lanes can use the existing shared
memory, FP, vector, and integer arithmetic services. Contributor
ownership and validation are in [DEVELOPING.md](DEVELOPING.md).

## Get started

```rhdl
import:
  lib("cores/rv3wide/rv3wide.rhdl").RV3Wide
  lib("cores/rv3wide/profile.rhm").RV3WideConfig
  lib("cores/common/cache/config.rhm").CacheConfig

inst core(RV3Wide(CacheConfig(64, 2), ~chi: config))
```

The default configuration selects scalar RV64IMACB with the shared system
features and Sv39, without FP or vectors. Configuration arguments and
architectural capabilities follow the shared implementation.

## Public contract and limits

`rv3wide.rhdl`, `hart.rhdl`, `profile.rhm`, and `udb.rhm`
export `RV3Wide`, `RV3WideHart`, `RV3WideConfig`, and
`rv3wide_udb_configuration`. `RV3WideConfig` is a distinct immutable class,
not an alias of RV2WideConfig or SuperscalarPipelineConfig. Its sole field is
`pipeline :: SuperscalarPipelineConfig`, defaulting to `SuperscalarPipelineConfig()`.
The shared type owns parameters, defaults, and validation; the wrapper exposes
only `isa_profile`, `csr_config`, and `hart_description` as metadata conveniences.
For example, `RV3WideConfig(SuperscalarPipelineConfig(~btb_entries: 16))`
selects a smaller BTB. Import the pipeline type from `cores/superscalar/profile.rhm`.
`RV3WideHart` independently
owns reset/start and CHI wiring. `RV3Wide` is a host factory that projects
configuration without adding pipeline hierarchy. For the generic execution slice with
XLEN-wide vector storage, use `SuperscalarCore(profile.pipeline, ~issue: RV3WideIssue(profile.pipeline.xlen))` from
`cores/superscalar/core.rhdl` directly. UDB uses the
same contained pipeline config; there is still only one execution implementation.

Fetch remains eight bytes per cycle, issue/retirement support three lanes, and the
GPR file is 4R3W. Pipeline payloads come from `cores/superscalar/bundles.rhdl`.
RV3Wide's coherent L1D reads a double-XLEN row per way: 64 bits on RV32 and 128 bits on RV64.
The LSU still issues one native word per request; a wider row does not issue
two scalar loads together. Page-table reads, misaligned fragments, and
device transactions keep their existing sizes.

Vector-enabled RV3Wide uses a 64-row 3R1W vector register file. Its row width
(DLEN) matches L1D, and VLEN is twice DLEN: 128 bits on RV32 and 256 on RV64. Register `vN` occupies rows
`2*N` and `2*N+1`, low bits first. The load, store, and execute sequencers each have an independent read of the two-row v0 shadow.
All see concurrent masked writes. VXSeq receives the predicate with its synchronous operands; load and store sample it during row preparation.
Checked instructions dispatch in program order directly into one-entry contexts:

| Instruction | Memory-side entry | VRF-side sequencer |
|---|---|---|
| Load | VLIQ: through delivery of the final cache response | VLSeq: through final write or skip |
| Store | VSIQ: through the final cache acknowledgement | VSSeq: through final operand capture or skip |
| Execute | — | VXSeq: through final irrevocable issue |

A native indexed access also reserves the special sequencer (VPS) for its
index stream; a native register permutation reserves VPS together with VXSeq.
A memory instruction reserves its address and VRF entries together. If any required entry is
busy, younger instructions cannot pass the blocked dispatch. Already admitted
instructions progress independently; a result may still be in flight after
its sequencer is free. There are no instruction FIFOs ahead of the sequencers.
Certified ordinary unit-stride loads and stores use a separate path between L1D
and the VRF when the base is element-aligned, whether masked or unmasked.
Loads assemble cache beats into register rows; stores
pack captured rows into byte-enabled cache beats. Each beat uses its own
translated physical page, even when consecutive virtual pages are not physically adjacent.
Certified mask transfers use byte offsets and round VL up to a whole byte.
Whole-register loads and stores use the encoded EEW and group size, not VL.
For certified unit-stride segments of two through eight fields, the same port
transfers interleaved bytes in memory order. VLSeq separates a beat with one
masked VRF write per active field; VSSeq gathers its fields into one beat.
A younger instruction cannot consume a partially written field row.
Strided, indexed, and fault-only-first segments stay on the element path.
Nonsegmented `vlse` and `vsse` use the same L1D port with a validated one-page
certificate, zero `vstart`, and an aligned base and nonzero element-aligned stride.
The address advances by the signed stride, including past masked elements,
but only active elements issue requests. Zero stride keeps its existing handling.
For single-field ei8 accesses the index stays one byte at every supported SEW.
A one-page certificate must cover all 256 possible starting addresses **and the
last byte of each element**. The native path requires disjoint VS2 and data
groups, disabled pointer masking, and zero `vstart`. An index row can supply
several data rows before another VRF read is needed. Gather and scatter share
one index sequencer: it can read the next index row on an unused VRF port cycle,
without issuing memory or taking the port from store data. A masked element uses no
credit. An active element crossing a DLEN boundary issues two masked beats.
Unordered accesses can have two beats outstanding; ordered accesses wait for
the preceding load response or store ACK.
Single-field wider-index instructions do not need this certificate. At WB the
checker reads unsigned VS2 offsets at their encoded width and checks only the
active elements, in order. Consecutive accesses wholly inside the same certified
4 KiB page reuse its mapping for this instruction. Masked-off elements neither
probe nor replace the page; an element crossing a page boundary uses an exact
two-page check. Once the VLSU accepts an element and its translations, the
checker can probe the next active element without waiting for L1D to grant
the first one. For ordinary cacheable mappings, up to two checked
unordered elements can be in flight. Within a certified page, unordered
gathers and scatters can issue adjacent elements on consecutive cycles when
their operands are resident and credits are available. Scatters reuse each
captured VS3 row; a following fence still waits for the store acknowledgments.
L1D requests still issue in element order;
a split access uses one beat credit per fragment, even across nonadjacent physical
pages. Ordered accesses may probe ahead but cannot issue the next element before
the previous VRF write or store ACK. A failed check resumes on the precise LSU
only after every earlier element drains, without repeating earlier stores.
Overlapping data/index groups stay on the precise path.
Each row enables only bytes selected by `vstart`, `vl`, and the element predicate;
a fully disabled physical beat advances without issuing a cache request. A failed range check falls back to
precise element accesses rather than raising a fault on a masked-off address.
VLIQ and VSIQ advance separate address cursors over one L1D port with two
shared row credits. The two start ports are independent: a blocked start cannot
hide an eligible head of the other kind. Delivery to the load sequencer ends the
memory dependency; older VRF readers can still delay the register write. A certified load and store can overlap in either order
only if their translated byte ranges are disjoint. Distinct virtual addresses
may alias; mask holes are conservatively treated as accessed. A load credit
lasts until the merger consumes its cache beat, a store credit through its ordered acknowledgement.
Scalar memory ordering and fences do not use this exception. A certified load can
fetch both rows while an older native instruction still needs their previous
VRF contents. Returned rows wait in order for older readers and writers and
the single write port; dependent consumers see each row only after its write.
VLSeq can capture the next row's predicate while an older aliasing store delays
address issue. After its last use of a v0 row, a younger instruction may overwrite
that row; the eventual request uses the captured byte mask.
Stores borrow an idle general read port and can buffer two DLEN-wide operand
rows independently of L1D. A store consuming an older load can capture its
first row while the second is still pending. A later VRF overwrite does not
change the buffered bytes. Capture is not a cache request and does not release
the translation window or order a fence. Loads share the single write port.
Native instructions chain through the VRF one physical row at a time: a consumer
can read a load or arithmetic result after that row is written, and a younger
writer can overwrite a store source row after capture. A repeatedly read v0
shadow row stays protected through its last use.
`vadd`, `vsub`, `vand`, `vor`, `vxor`, ordinary shifts (`vsll`, `vsrl`, `vsra`),
and signed or unsigned integer min/max (`.vv` and `.vx`) use the native execute
pipe for nonempty ranges, including masked and partial rows. Same-width saturating
and averaging add/subtract and rounded right shifts use the same two-cycle path.
Averaging and shifts use the instruction's captured `vxrm`. Only saturation in
an enabled element sets `vxsat`, on the destination row's VRF write. With Zvbb enabled,
all Zvbb operations use this pipe, including counts, reversals and widening shifts.
VXSeq also runs reverse subtract, widening add/subtract (including wide VS2),
sign/zero extension, and narrowing shifts and clips. It issues one physical row
of the widest operand per grant. Narrowing therefore takes two grants for a
full destination row; consumers can chain after its last required fragment
writes. No old-destination read is needed for a masked or partial write.
It can accept one DLEN-wide
row per cycle on either XLEN, including across independent native instructions:
the final operand read can admit the next instruction without waiting for writeback.
Dependent rows still wait for the producer's VRF write. Masked rows read the v0 shadow alongside the
operands. Bytes before `vstart`, past the active range, or disabled by v0 keep
their previous values. The eight mask-register logical operations also use VXSeq,
with one bit per element regardless of SEW or LMUL. A masked consumer can use
the first updated row of v0 while its producer is still waiting to write the second.
Integer reductions, including widening sums, keep their intermediate results
private and write only element zero. For a nonempty, fully masked reduction
that element is the seed; an empty reduction leaves the destination untouched.
Mask-prefix operations, `viota.m`, and `vid.v` chain by destination row.
`vcpop.m` and `vfirst.m` instead return a single GPR result; at zero VL those
results are zero and −1, respectively.
Integer comparisons also run on VXSeq. Each source row writes its comparison
bits directly into the destination mask, preserving inactive bits even when
the destination is v0. A consumer can read that mask row only after its last
fragment has written back.
The same adder implements `vadc` and `vsbc` (data results) and `vmadc` and
`vmsbc` (mask results). For these operations v0 supplies per-element
arithmetic inputs, never write enables. The no-input forms of `vmadc` and
`vmsbc` do not wait for an unrelated write to v0.
Elementwise integer `vmerge` writes either VS2 or the second operand for
every element in range; v0 selects the value, not whether to write it.
`vmv.v.v` reads only the named source, while `vmv.v.x` and `vmv.v.i`
broadcast without a general vector-register read.
Whole-register copies stream from VS2 to VD one row at a time; a dependent
store may capture a completed row while the rest of the copy is in flight.
Element-zero extracts reserve a scalar GPR or FPR even at zero VL. Inserts do
not read old VD and skip the write when `vstart >= vl`.
At supported FP element widths, `vfmerge.vfm` and `vfmv.v.f` use the same
selector and broadcast paths. They check NaN boxing on the scalar operand,
copy vector lanes bit-for-bit, and do not issue FP arithmetic or update
`fflags`.

VXSeq sequences same-width FP add/subtract, multiply, fused operations, comparisons,
min/max, sign injection, classification, estimates, and integer conversions. FP32 and
supported FP16 use packed HardFloat lanes. Widening add/subtract, multiply and FMA,
and widening/narrowing conversions also use VXSeq. The widest operand or result
selects the datapath: a 16↔32 operation stays packed, while a 32↔64 operation
sends active elements to the shared scalar double pipe. A narrowing write enables
only its destination fragment; the row stays claimed through the final VRF write.
`vfmacc.vv` shares the third VRF read port fairly with store-data capture;
`vfmacc.vf` needs only two VRF reads and can issue alongside a store-data read.
Packed FP leaves the scalar FP pipe available; only active lanes accrue exception flags.
Same-width integer `vmul`, `vmulh`, `vmulhu`, `vmulhsu`, `vmacc`, `vnmsac`,
`vmadd`, and `vnmsub` (.vv/.vx) use the DLEN-wide SIMD multiplier when SEW
is narrower than XLEN. Integer MACs read old `vd`: a .vv MAC competes with
store-data capture for the third port, while a .vx MAC leaves it free. A younger
independent add can write before a multiply. Each gets its VRF write cycle at
operand issue; a collision stalls the new read, not the returning result.
`vwmul`, `vwmulu`, `vwmulsu`, `vwmacc`, `vwmaccu`, and `vwmaccsu` (.vv/.vx),
plus `vwmaccus.vx`, also use this multiplier for sub-XLEN sources. Each MAC
reads old `vd` at the widened element width. Each narrow row can produce two widened rows: consumers can
use the first after its write, but writers must wait for the last read of the
narrow source. Sub-XLEN `vsmul.vv` and `vsmul.vx` use the same multiplier:
each instruction rounds using its own `vxrm`, and only an active saturated
element sets `vxsat` at row writeback. Full-XLEN products use the shared scalar multiplier.
For all supported same-width full-XLEN products (low, high, MAC, and fractional),
VXSeq reads each row once and sends only active elements to the shared multiplier.
Independent arithmetic can start once the final operand row is granted, even
if the scalar multiplier is not accepting requests; a consumer
of the product waits for the row's VRF write. Fractional saturation updates
`vxsat` on that write, not when the scalar reply arrives.
VXSeq also runs `vdiv`, `vdivu`, `vrem`, and `vremu` in both `.vv` and `.vx`
forms. Every supported SEW uses the shared scalar divider. A masked, prestart,
or tail element sends no request and preserves its destination bytes. The
divide adapter sequences active elements after the read; the destination row
remains claimed through VRF writeback. On an uncontested final read, VXSeq can
accept the next instruction on that same edge. An independent add can read
on the following cycle even if the integer or FP divider is stalled.
Integer `vslideup` and `vslidedown` (.vi/.vx) use the special sequencer to align
adjacent source rows for VXSeq. Scalar and immediate register gathers broadcast
one element; `vrgather.vv` and `vrgatherei16.vv` can revisit any source row
and build one destination row element by element. On a slide or gather's final
row grant, a following slide or gather can enter VPS and VXSeq together. Its
first independent source read can issue the next cycle; a read of the outgoing
destination still waits for its VRF write. An instruction needing only VXSeq
can also enter on the final grant. Integer and FP slide-one
insert the scalar at element zero (up) or `vl-1` (down); FP insertion checks
NaN boxing without raising FP flags. `vcompress.vm` packs selected elements in
source order and writes only the packed prefix. When its end-of-scan token is
accepted, a following slide, gather, or compress can take both sequencers on
that edge. An all-zero selection needs no destination write or reservation.
VXSeq folds all six FP reductions in element order, starting from `vs1[0]`.
It writes only `vd[0]`; an all-masked nonempty reduction copies the seed without
raising flags, while zero VL leaves the destination untouched. For `vfdiv`,
`vfrdiv`, and `vfsqrt`, VXSeq sends only active elements to the shared scalar
divider, which can accept scalar work between vector elements. VXSeq holds
the completed row and its flags until VRF writeback without holding the divider
or reserving a write cycle. Noneligible memory transfers still use the XLEN sequencer.

There is no backend lane-count parameter. `issue.rhdl` exports
`RV3WideIssue(xlen)`, a three-candidate scheduling policy with bounded read-path
borrowing and fixed per-lane write ports. The factory supplies it to the shared
core. Lane 3 prefers lane 2 rs2's read/bypass path for rs1 and lane 1 rs2's path
for rs2, only when those donor operands are unused or x0. Matching sources first
share any active older read path; equal lane-3 sources share one allocation.
A sole unmatched source can fall back to the other free donor; two distinct
unmatched sources keep fixed routes. Issue selects
an older move's captured read path when a same-group copy
dependency permits sharing; this works even when the usual donor is occupied.
Zero-offset scalar loads/stores in lane 3 may select lane 2's ALU result as their
address without a register-read path or a second dependent addition. Other
operands keep their normal interlocks.
Three distinct direct GPR writers can coissue. WB suppresses an older
same-destination write only when the younger instruction actually writes;
retirement records remain separate. A faulting, replayed, or delayed load
preserves the older write. Deferred returns share lane 3's port, reserving that
lane ahead of time while lanes 1 and 2 can continue issuing and writing.

All three lanes can execute branches, loads/stores, FP, vector, and multiply/divide
operations. This does not add execution-service bandwidth: same-group conflicts,
operand borrowing, destination hazards, and write-port availability still limit
admission. Serializing system commands still issue alone. Fixed arithmetic can
launch speculatively in EX; WB authorizes architectural effects after every older
lane succeeds. Fault/replay in any lane preserves its successful older prefix
and suppresses younger effects. Vector memory retains its own owner when needed;
only its unretired younger suffix is replayed.

RV3Wide has the same supported shape/ISA selections and CI enrollment as RV2Wide:
Mini enrolls `rv32int`, `rv32max`, `rv64imacb`, `rv64imafdcb`, and `rva23`;
Simple additionally enrolls `rv64max`; Tiled4Core and Tiled8Core enroll `rva23`.
All four RV64 presets remain supported manual selections on both tiled shapes,
and Mini RV64Max remains a manual selection. Use complete keys such as
`mini-rv3wide-rv64imacb` or `simple-rv3wide-rva23`.

Software coverage follows the shared shape/ISA policy, including Mini/Simple
Sail cosim, Simple ISA/ACT suites, and tiled multihart workloads. The existing
shared `rv3wide-fetch` fixture also exercises this entry point
against the same behavioral oracle as RV2Wide. See the
[shared contract](../superscalar/README.md) for all other interfaces and limits.
