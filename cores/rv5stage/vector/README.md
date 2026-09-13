<!-- Defines experimental vector configuration, decode, storage, and execution-boundary contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Experimental vector path

The opt-in `RVCoreProfile(~experimental_vector: vlen)` enables configuration,
vector CSR state, and the decoded same-width integer subset. The default is
`#false`. Neither setting advertises `V`, Zve, or Zvbb; vector memory and the
remaining vector instruction families are not implemented.
The reusable arithmetic stays in [`SimdALU`](../../README.md#packed-simd-integer-alu).

## Configuration and decode

The experimental core executes `vsetvli`, `vsetivli`, and `vsetvl` through the
existing serializing system-instruction path. Only WB updates `vl`, `vtype`,
and `vstart`; scalar `rd` receives the new VL. Squashed or faulting operations
do not update this state. Supported geometry is ELEN=64, SEW 8/16/32/64,
LMUL 1/8 through 8, subject to SEW <= LMUL * ELEN. Unsupported configurations
set `vill` and zero VL. Ordinary AVL selection uses `min(AVL, VLMAX)`;
`rs1=x0,rd!=x0` selects VLMAX, while `rs1=rd=x0` preserves VL only when the
old/new types are legal and VLMAX is unchanged. Reserved keep-VL uses trap.

The CSR bank exposes `vstart`, `vxrm`, `vxsat`, `vcsr`, `vl`, `vtype`, and
`vlenb`. `vstart` retains enough low bits for VLEN-1; `vxrm` and `vxsat` alias
`vcsr`. VL/type/VLENB are read-only. Reset starts with `vill=1`, VL=0, and
`mstatus.VS=Off`; VS Off blocks vector CSR access and configuration. Successful
configuration, vector CSR writes, or integer macro retirement mark VS Dirty;
reads do not, and SD combines
the FP and vector dirty states. Software may manage VS through M/S status.

The [vector control column](../decode/vector-ctrl.rhdl) describes same-width
add/sub, logic, shifts, comparisons, and min/max using direct SIMD controls,
operand selection, comparison inversion, and operand swapping. Runtime group
checks cover alignment, fractional groups, masked data destinations, and
mask-result overlap. Legal rows execute through the Decode-held integer
unroller. VS Off, `vill`, and invalid register groups trap before unrolling.
This is an initial subset of [RVV 1.0](https://docs.riscv.org/reference/isa/unpriv/v-st-ext),
not a complete vector ISA implementation.

## Integer pipeline and unroller boundary

[`RV5StageVectorPipeline`](../vector.rhdl) contains the unroller, 3R1W vector
register bank, packed SIMD execution, and private EX/MEM/WB data registers.
Its `request` accepts a legal macro snapshot. Each accepted `issue` emits only
the caller's context and a `last` marker, atomically capturing that beat's
operands in the private pipeline. Scalar stages carry bookkeeping, not vector
operands or register-write payloads.

The nonstallable `commit: Valid(Bits(XLEN))` supplies the macro PC and authorizes
the corresponding result exactly three cycles after issue. No authorization
means no write. `cancel: Pulse` discards speculative work and flushes private
stage validity; it does not undo a live older WB authorization on that edge.
The caller must suppress authorizations for squashed tokens. `retire: Pulse`
reports the authorized last beat. `active` stays asserted through WB drain.
These are fixed-cycle paired pipelines, not independently queued completions.

[`bundles.rhdl`](bundles.rhdl) defines an instruction/configuration snapshot,
64-bit packed micro-ops, and WB authorization/retry/fault feedback. Position
is an exclusive architectural element range, independent of masked-off lanes;
caller-defined context identifies outstanding work. Authorization is distinct
from result completion, and accepted side effects must never be retried.
[`RV5StageVectorUnroller`](unroller.rhdl) retains one macro descriptor and its
scalar/configuration snapshot. Younger instructions wait in Decode until its
last WB beat; older scalar instructions can finish or squash it normally.
Three synchronous VRF reads supply `vs2`, `vs1`, and `v0`. A two-slot credit
window reserves space before every read, covering read latency and buffered
beats even when issue stalls. Once filled, it supplies one packed 64-bit beat
per cycle. A macro has setup/drain latency; this is not single-cycle vector
instruction issue.

The vector pipeline's private EX stage uses
[`RV5StageVectorExecute`](execute.rhdl) and the shared SIMD ALU. Its MEM/WB
registers retain the packed result; only scalar WB authorization writes the
VRF. An exclusive end position advances even for masked-off
elements. Only the final beat retires the macro, advances architectural PC,
consumes an NTL hint, clears `vstart`, and marks VS Dirty. Interrupt entry waits
for the macro to drain. A zero-length body or `vstart >= vl` emits one empty
completion beat, with no register write.

At the low-level unroller boundary, issued and authorized positions are
separate. Ordered WB feedback
identifies the oldest unauthorized beat and the macro PC. Retry flushes all
pending reads/issue beats and restarts at the authorized frontier, preserving
already committed writes and the initial partial-chunk enable floor. The
caller must discard younger downstream beats on retry/cancellation; the
unroller does not own EX/MEM/WB. The composed integer vector pipeline cannot
reject a vector write, so its public interface accepts only commit/cancel and
it generates authorization feedback internally. Fault
feedback and cancellation terminate the retained macro; a future faulting LSU
must additionally own architectural `vstart`/trap handling.

This cut preserves inactive and tail contents, supports fractional LMUL and
in-place same-width groups, sign-extends RV32 VX operands before SEW64
broadcast, and packs comparison bits through the ordinary masked write port.
Shared FP/multiply/divide issue and vector memory ordering remain separate
future integration work.

## Register bank

`RV5StageVectorRegisterFile(vlen :: VectorLength)` is a **3R1W** bank with exactly
`32 * VLEN / 64` entries of `Bits(64)`, with no reset value. VLEN is a host
power of two from 128 through 65536 bits. The flat address is
`register_number * (VLEN / 64) + chunk_number`; chunk zero holds the lowest bits.
`v0` is writable, not a hardwired zero register.

- Three independent `Valid(Address)` reads return `Valid(Bits(64))` exactly one
  cycle later. Operand and mask reads share these three ports. There is
  no backpressure; the caller must have space for every requested result.
- One `Valid(VectorRegisterWrite(vlen))` write carries an address, 64-bit data,
  and **64 individual bit enables**. Ordinary byte enables are expanded by the
  result adapter. Mask results update individual bits through the same port.
- A read and write sampled at the same edge return the post-write value:
  enabled bits forward new data, disabled bits retain their prior values.
  Later writes cannot change an already captured read response.
- Synchronous reset clears response validity and suppresses writes. It does
  not initialize or erase architectural storage.

The implementation is a flat register array with registered read results, not
a promise of an inferred technology SRAM. This gives precise collision and
masked-write semantics without depending on an unspecified SRAM collision mode.

## Packing boundary

`RV5StageVectorOperands(vlen)` takes two source chunks, one mask chunk, a scalar,
a five-bit immediate, and `VectorPackingControl(vlen)`. It emits SIMD operands,
element enables, and the first output element. This two-source ALU path uses
the third bank read for the mask. A future masked three-source operation must
schedule mask capture through the same three ports, not add a fourth port.

`first_element` denotes the start of an aligned **source** chunk, not the next
enabled element. The caller supplies the mask word containing that element
(`v0` chunk `first_element / 64`). Enables intersect `vstart <= i < vl`,
`i < vlmax`, and the architectural mask. Nonzero `vstart` suppresses lanes rather
than shifting their positions. Scalar/immediate broadcast uses the low SEW
bits; immediates select signed or unsigned extension explicitly.

Widening reuses `SimdWidenOperands`: each invocation zero-extends either half
of an 8/16/32-bit source chunk into one 64-bit output chunk. The returned first
element and width describe that destination chunk. Both halves must use the
appropriate source snapshot. Signed widening, narrowing, saturation, reduction,
and permutation scheduling are not supplied by this adapter.

`RV5StageVectorResult(vlen)` converts a SIMD result into the bank write payload.
Data destinations use the returned output element width; comparisons place one
bit per element into the correct destination mask chunk. Disabled bytes/bits
receive no write enable, preserving inactive and tail contents.

Both adapters expose `legal` for their local chunk/alignment/bank bounds. The
caller must gate writes with it and separately validate architectural register
groups, EEW/EMUL, `vtype`, overlap, and instruction-specific restrictions. The
pure [`VectorConfig`](../../../riscv/README.md#vector-geometry) supplies host
geometry and ordinary data-overlap rules, not an instruction decoder.

Writes are authorized inputs. The bank and packing adapters do not decide WB
permission or cancel accepted work; the core's WB boundary owns that decision.

See [DEVELOPING.md](DEVELOPING.md) for ownership and focused validation.
