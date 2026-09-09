<!-- Defines the dependency boundary and public API of the RISC-V-to-Rhodium adapter. -->

# RISC-V/Rhodium adapter

`riscv/rtl` is the only bridge from the [pure RISC-V model and ISA catalogs](../README.md)
to hardware. It turns architectural descriptors into public Rhodium values and
reusable circuits without acquiring concrete-core, pipeline, fetch, retirement,
interconnect-topology, CIRCT, or test policy.

Contributors changing this adapter should read
[`DEVELOPING.md`](DEVELOPING.md).

## Find what you need

| Task | Start here |
|---|---|
| Build decode patterns from architectural encodings | [`instruction-pattern.rhdl`](instruction-pattern.rhdl) |
| Extract fields or immediates from a 32-bit instruction | [`instruction-fields.rhdl`](instruction-fields.rhdl) |
| Expand a 16-bit C instruction | [Compressed-instruction expansion](#compressed-instruction-expansion) |
| Define CSR recognition, reads, and writes | [CSR values and state](#csr-values-and-state) |
| Add cycle and retired-instruction counters | [CSR values and state](#csr-values-and-state) |
| Materialize trap, interrupt, PMA, or Sv39 policy inputs | [Privilege, memory, and translation values](#privilege-memory-and-translation-values) |
| Apply RISC-V floating-point representation rules | [Floating-point policy](#floating-point-policy) |
| See runnable decode examples | [Examples and validation](#examples-and-validation) |

## Dependency contract

Adapter modules may import `riscv/model`, `riscv/isa`, public `#lang rhodium`
libraries, and the public HardFloat package. They must not import Rhodium
implementation modules, concrete processors, CIRCT, examples, or tests. The
dependency direction is shown in the [package overview](../README.md#dependency-boundary).

The adapter owns representation conversion and reusable architectural hardware.
Concrete cores own instruction selection, decode outputs, register files,
pipeline and fetch behavior, privilege transitions, CSR policy, scheduling,
execution, and retirement. See [`../../cores/`](../../cores/README.md) for those
integration boundaries.

Dependency enforcement and extension workflow are documented in
[`DEVELOPING.md`](DEVELOPING.md#architecture-and-dependency-boundary).

## Component map

| Module | Public surface | Contract |
|---|---|---|
| [`instruction-pattern.rhdl`](instruction-pattern.rhdl) | `encoding_pattern`, `instruction_pattern` | Convert pure value/care images to typed Rhodium `Pattern`s |
| [`instruction-fields.rhdl`](instruction-fields.rhdl) | `instruction_field`, `immediate_bits`, `instruction_immediate` | Materialize descriptor-owned slices and extended immediates |
| [`compressed.rhdl`](compressed.rhdl) | `RiscvCompressedExpansion`, `RiscvCompressedExpander`, `compressed_selector_cases` | Recognize legal C encodings and emit canonical 32-bit instructions |
| [`mop.rhdl`](mop.rhdl) | `resolve_mop_decode_cases` | Compatibility name for the standard decode-overlay operation |
| [`csr.rhdl`](csr.rhdl) | `CsrBank`, `csr_bits`, `csr_bank` | Convert `CsrId` and define exact-key CSR recognition, reads, and writes |
| [`cmo.rhdl`](cmo.rhdl) | `CboManagementOperation`, `CboInvalidateMode`, `CboManagementPermission`, and `cbo_*`/`cmo_*` helpers | M/S/U CMO permission, invalidate-to-flush conversion, xenvcfg WARL fields, and physical permission |
| [`privilege.rhdl`](privilege.rhdl) | `PrivilegeMode`, `effective_data_privilege` | Shared M/S/U values and MPRV/MPP selection for explicit accesses |
| [`pointer-masking.rhdl`](pointer-masking.rhdl) | `PointerMaskMode`, `PointerMaskControl`, and pointer-mask helpers | RV64 Ssnpm WARL controls and explicit-address normalization |
| [`counters.rhdl`](counters.rhdl) | `RiscvCounterWrite`, `RiscvBaseCounters` | Reusable 64-bit `mcycle` and `minstret` state for RV32/RV64 |
| [`trap.rhdl`](trap.rhdl) | `exception_cause_bits` | Convert architectural synchronous causes to width-specialized hardware |
| [`interrupt.rhdl`](interrupt.rhdl) | `interrupt_cause_bits` | Convert architectural interrupt causes to `xcause` values |
| [`pma.rhdl`](pma.rhdl) | `RiscvPhysicalMemoryAttributes`, `RiscvPhysicalMemoryRegion`, `RiscvPhysicalMemoryMap`, `RiscvPhysicalMemoryLookup` | Validate host-authored regions and perform hardware access lookup |
| [`sv39.rhdl`](sv39.rhdl) | `Sv39Access`, `Sv39Pte`, `Sv39Translation`, and `sv39_*` helpers | Materialize Sv39 geometry, demand permission, and A/D-independent prefetch permission as typed hardware |
| [`floating-point.rhdl`](floating-point.rhdl) | `FloatSignOperation`, `RiscvRoundingMode`, Zfa immediate constants, and `riscv_*` helpers | Apply RISC-V policy around HardFloat values |

## Decode descriptors

### Patterns

[`instruction-pattern.rhdl`](instruction-pattern.rhdl) converts either an
`InstructionEncoding` or an `InstructionSpec` to a Rhodium `Pattern` with the
descriptor's exact width, value, and care mask. The conversion is catalog
independent, so 16-bit compressed encodings and 32-bit RV32/RV64 encodings use
the same boundary.

Generic case grouping, output patterns, valid-tagged partial mappings, and
hardware decode generation remain owned by
[`rhodium/std/decode`](../../rhodium/std/README.md#typed-decode). The RISC-V
adapter supplies architectural input patterns; it does not define generated
control signals.

### Fields and immediates

[`instruction-fields.rhdl`](instruction-fields.rhdl) turns pure `BitField` and
`ImmediateLayout` descriptors into hardware slices and concatenations.
`instruction_field` requires the described field to fit a 32-bit input word.
`immediate_bits` reconstructs the descriptor's exact immediate width, including
implicit zeros, while `instruction_immediate` sign- or zero-extends it to a
caller-selected width that is at least the layout width.

Architectural placement therefore remains defined once in `riscv/model`;
consumers select a descriptor instead of copying slice maps.

## Compressed-instruction expansion

[`compressed.rhdl`](compressed.rhdl) defines the combinational
`RiscvCompressedExpander(xlen, floating_point, compressed_extensions,
~supported_instructions: names)` circuit.
Its input is `Bits(16)` and
its `RiscvCompressedExpansion` output contains `valid: Bool` plus the canonical
`instruction: Bits(32)` for the existing 32-bit decoder.

`ZcaCompressedExtensions` always selects only the XLEN-appropriate Zca
catalog. `CCompressedExtensions` selects this exact architectural composition:

| `XLen` | Floating-point profile | Included compressed catalogs |
|---|---|---|
| `XLen.X32` | `None` | RV32Zca |
| `XLen.X32` | `F` | RV32Zca + RV32Zcf |
| `XLen.X32` | `D` | RV32Zca + RV32Zcf + RV32Zcd |
| `XLen.X64` | `None` or `F` | RV64Zca |
| `XLen.X64` | `D` | RV64Zca + RV64Zcd |

The circuit derives its selector relation from the pure descriptors and lowers
their nonzero-field and nonzero-immediate legality constraints into disjoint
accepted input patterns before hardware decode,
validates their required targets against the supplied canonical 32-bit
instruction names, and materializes their target operand and immediate
bindings. Optional Zcb descriptors are selected from the same target catalog,
implementing Zbb, Zba, and M/Zmmul prerequisites without duplicating feature
flags. Optional Zcmop descriptors then occupy the C.LUI zero-immediate holes
without a priority decoder. An unmatched,
reserved, or unsupported encoding deasserts `valid`; consumers must use
`valid` to qualify the instruction bits. Architectural hints that the pure
catalog accepts remain valid and expand to their canonical no-effect base
instruction. There is no parallel operation enum or handwritten opcode table.

The standard decode library's `overlay_decode_cases` helper supports extensions
that redefine only part of a broader fallback encoding. It subtracts explicit
override input sets from fallback rows, returning one disjoint relation. Core
decode therefore need not depend on row priority when an extension assigns
architectural behavior to an existing hint or fallback region. `mop.rhdl`
retains `resolve_mop_decode_cases` as a compatibility name for existing MOP
consumers; new generic composition should use the standard helper directly.

The [pure-model guide](../README.md#compressed-instruction-expansion) explains
the shared host and hardware expansion path.

## CSR values and state

[`csr.rhdl`](csr.rhdl) converts a pure `CsrId` to its typed `Bits(12)` address.
The `csr_bank` form defines each implemented identifier once and derives:

- `recognized`, true only for a listed exact key;
- `read_value`, selected from a required nonempty entry list with a caller-owned
  default;
- `write(value)`, which dispatches only to entries that accept writes.

`storage` entries read and directly replace state, `read` entries are constants
or write-ignored views, and `csr` entries provide custom read/write behavior
for aliases and WARL masking. `read_all IDs: value` assigns one read value to
each identifier in a host list of `CsrId` values, with writes ignored. For
example, `read_all MachineHpmCounterIds: bits(0, width)` describes the permitted
zero-valued HPM counter bank without allocating registers. Grouped entries
participate in the same duplicate-identifier checks as individual entries;
the caller still owns architectural read-only and privilege checks. The pure ISA
package continues to own identifier names and numeric addresses.

[`counters.rhdl`](counters.rhdl) implements 64-bit `mcycle` and `minstret` state.
RV32 writes preserve the untouched half; an explicit machine-counter write has
priority over the same instruction's implicit increment. The integrating core
supplies the precise `retire` event and owns CSR recognition, privilege and
`counteren` gating, and the platform `time` source.

## Privilege, memory, and translation values

[`pointer-masking.rhdl`](pointer-masking.rhdl) implements the
[ratified pointer-masking transformation](https://docs.riscv.org/reference/isa/v20260120/priv/zpm.html).
`user_pointer_mask_control` selects `senvcfg.PMM` for effective U-mode accesses
(including MPRV), with MXR suppressing masking even in Bare mode.
`apply_pointer_mask` replaces the upper 7 or 16 bits with zeros for physical
addresses or the next bit's sign for virtual addresses; disabled mode and
RV32 preserve the address. Remaining canonicality and permission checks belong
to the downstream translation and memory system.

`pointer_mask_envcfg_fields` returns only PMM bits, allowing CSR owners to
combine it with other `senvcfg` fields. Reserved PMM=01 normalizes to disabled;
PMM=00/10/11 select PMLEN=0/7/16. The pure definitions live in
[`../isa/pointer-masking.rhm`](../isa/pointer-masking.rhm).
Apply masking once to explicit memory effective addresses, including prefetches,
atomics, FP memory operations, and CMOs. Do not apply it to instruction fetch,
implicit PTE accesses, branch targets, or software CSR values. Hardware address
faults must retain the transformed address for `xtval`.

[`trap.rhdl`](trap.rhdl) and [`interrupt.rhdl`](interrupt.rhdl) convert pure
architectural causes to caller-selected widths. The interrupt form sets the
top `xcause` bit. Cause selection, pending sources, priority, delegation, and
privilege transitions remain core policy.

[`pma.rhdl`](pma.rhdl) defines stable host parameters for nonoverlapping
physical-memory regions and their read, write, execute, cacheable, atomic,
device, and read-idempotent attributes. Cacheable regions cannot also request
device-memory transaction semantics. `RiscvPhysicalMemoryMap.lookup(first,
last)` accepts arbitrary-width hardware addresses, rejects high bits outside
the configured physical width, and reports attributes only when both endpoints
lie in the same region. A transfer therefore cannot straddle PMA regions.
The optional `~cache_block_zero` attribute defaults to false and explicitly
permits cache-block zero accesses independently of cacheability. Callers must
also check write permission and pass the complete aligned block range to
`lookup`. Interconnect routing and Home ownership remain outside this adapter.

[`sv39.rhdl`](sv39.rhdl) materializes the pure Sv39 constants as typed PTE,
access, translation, canonical-address, VPN, leaf, structural-validity,
superpage-alignment, permission, prefetch-permission, and physical-address
helpers. `sv39_prefetch_permission_valid` accepts the union of fetch, load, and
store permission under current privilege, `SUM`, and `MXR` while deliberately
ignoring `A` and `D`. Page-table walk
state, TLB organization, replacement, faults, and processor integration are
not part of this reusable combinational layer.

`Sv39Pte()` uses the 64-bit PTE encoding as its packed representation. Convert
raw `Bits(64)` with `raw.into(Sv39Pte())`; use `pte.as_bits()` to recover the
encoded bits. These conversions preserve the bit pattern and do not validate
the PTE; structural and permission checks remain separate.

### Cache-block permissions

[`cmo.rhdl`](cmo.rhdl) separates CSR permission from translation and physical
permission. Its `cbo_management_permission` helper takes **current** M/S/U
execution privilege as `user_mode` and `supervisor_mode` (both false means M),
not MPRV's effective data privilege. M bypasses xenvcfg restrictions; S obeys
menvcfg; U obeys both menvcfg and senvcfg. CBIE enables invalidate or converts
it into a flush, with either applicable flush setting taking precedence.
CBCFE gates clean/flush independently of CBIE. A false `~zicbom` denies all
three instructions even in M-mode. Hypervisor modes are outside this helper.

`cmo_envcfg_fields` returns only the implemented CBIE/CBCFE/CBZE fields for
RV32 or RV64. It maps reserved CBIE=10 writes to 00 and zeros fields for
disabled extensions. It does not gate writes to one xenvcfg based on another;
the hierarchy applies at instruction execution. Callers merge any other
implemented xenvcfg fields separately. `cbo_zero_permitted` provides the
corresponding hierarchical CBZE rule.

`Sv39Access.CacheManagement` uses the **effective data-access privilege**,
SUM, and MXR. It permits load or store access, requires A, and ignores D.
As with the other access classes, PTE validity and leaf structure are checked
separately. `cbo_management_physical_permission` accepts a mapped block with
read or write permission regardless of cacheability, device classification,
atomic support, or CBO.ZERO capability. Supply the complete aligned block to
the physical-map lookup; PMP permission remains a separate caller check.

The integrating core must classify a denied CSR operation as illegal, a
translation denial as a store page fault, and a physical denial as a store
access fault. Keep the original rs1 value for tval, align only the maintenance
address, and do not generate an alignment exception. These helpers neither
issue a transaction nor enable Zicbom in a processor profile. RV5Stage's
[opt-in integration](../../cores/rv5stage/README.md#cache-block-management)
owns decode, precise retirement, and CHI execution. See the
[CMO specification](https://docs.riscv.org/reference/isa/unpriv/cmo.html).

## Floating-point policy

[`floating-point.rhdl`](floating-point.rhdl) translates between HardFloat types
and RISC-V architectural conventions. It owns canonical NaNs, NaN-box creation
and validation, invalid-box substitution, static and dynamic rounding-mode
selection, exception-flag mapping, raw moves, classification, sign injection,
and the specified min/max NaN and signed-zero behavior.

HardFloat owns arithmetic implementation. A concrete processor owns its FP
register file, `fcsr` state, decode, scheduling, execution-unit composition,
writeback, and retirement.

## Examples and validation

The executable adapter examples are
[`../../examples/riscv/instruction-pattern.rhdl`](../../examples/riscv/instruction-pattern.rhdl)
and
[`../../examples/riscv/instruction-fields.rhdl`](../../examples/riscv/instruction-fields.rhdl).

From the repository root, run the focused pure-model, catalog, adapter,
compressed-expansion, and boundary checks with:

```sh
make riscv-test
```

The focused test inventory and adapter-change workflow are in
[`DEVELOPING.md`](DEVELOPING.md#focused-validation).
