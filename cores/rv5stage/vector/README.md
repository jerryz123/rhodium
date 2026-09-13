<!-- Defines the standalone RV5Stage vector register bank and packed execution adapters. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Vector storage and packing

These standalone components prepare RV5Stage's vector execution path. They do
not add vector instructions, CSRs, retirement, or a `V`/Zvbb profile claim.
The reusable arithmetic stays in [`SimdALU`](../../README.md#packed-simd-integer-alu).

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

Writes are authorized inputs. These modules do not decide WB permission or
cancel accepted work. A future unroller must retain source snapshots where
needed and emit writes only for nonspeculative, authorized micro-ops.

See [DEVELOPING.md](DEVELOPING.md) for ownership and focused validation.
