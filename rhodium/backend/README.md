<!-- Documents the optional CIRCT backend and its lowering of verified public Rhodium IR. -->

# CIRCT backend

The backend is the CIRCT-specific consumer of Rhodium's public hardware IR. The
normal entry point accepts a completed `Design`, verifies it, and emits textual
CIRCT MLIR. This directory owns CIRCT dialect selection, type representation,
SSA names, and operation dispatch; it imports [`../core/`](../core/README.md)
but no frontend syntax or elaboration modules.
Contributors changing lowering or backend coverage should read
[`DEVELOPING.md`](DEVELOPING.md).

```mermaid
flowchart LR
    IR["Public Rhodium IR"] --> Verify["Core design verification"]
    Verify --> Backend["CIRCT backend<br/>type and operation lowering"]
    Backend --> MLIR["CIRCT MLIR<br/>hw, comb, seq, sv, verif, sim"]
    MLIR --> Passes["External CIRCT passes<br/>and ExportVerilog"]
    Passes --> SV["SystemVerilog"]
```

Rhodium stops at CIRCT MLIR; CIRCT's lowering passes and `ExportVerilog` own
SystemVerilog generation.

## Emission API

[`circt.rhm`](circt.rhm) exports two functions:

- `emit_circt(design)` is the whole-design API. It calls `verify_design`,
  collects design-wide record aliases and DPI declarations, emits every
  `hw.module`, and wraps the result in a builtin MLIR `module`.
- `emit_module_circt(module_def)` emits one `hw.module`. It does not run design
  verification or establish the design-wide record-alias scope, so callers
  producing a complete design should use `emit_circt`.

An unsupported verified type or opcode is a backend error. The backend does not
add pseudo-CIRCT operations to avoid an explicit lowering decision.

## Type representation

| Rhodium type | CIRCT representation |
|---|---|
| `ScalarDataType` | Signless integer with the type's physical width |
| `Clock`, `Reset` | `i1` in clock and reset positions |
| Anonymous `RecordType` | Recursive packed `hw.struct`, preserving field names and order |
| Named record shape | `hw.typedecl` plus `hw.typealias` in `@rhodium_types` |
| `VectorType` | Recursive `hw.array` |

Frontend-defined flat types therefore need no backend-specific case. A
representation cast whose lowered source and destination types are identical
is an SSA alias; another equal-width packed cast becomes `hw.bitcast`.

A record's preferred name is non-semantic metadata. The whole-design emitter
collects concrete named shapes into one type scope, reuses an alias for an
identical shape, and assigns stable numeric suffixes when distinct shapes ask
for the same name. Anonymous records remain inline structs.

## Lowering by concept

### Structure and state

| Rhodium IR | CIRCT lowering |
|---|---|
| `rtl.input_port`, `rtl.output_port`, `rtl.drive` | `hw.module` signature and `hw.output` |
| `rtl.instance` | `hw.instance` |
| `rtl.wire` | SSA alias to its verified driver |
| `rtl.cast` | SSA alias or `hw.bitcast`, according to lowered type equality |
| `rtl.register` | `seq.firreg` after `seq.to_clock` |
| `rtl.register_reset` | `seq.firreg` with active-high synchronous reset |

Inputs, outputs, drives, and wires need no standalone operation after their
connections have been incorporated into the module signature, `hw.output`, or
the consuming SSA reference.

### Primitive dataflow

| Rhodium IR | CIRCT lowering |
|---|---|
| `rtl.constant` | `hw.constant` |
| `rtl.dont_care` | `sv.constantX` |
| `rtl.not` | `comb.xor` with an all-ones constant |
| `rtl.and`, `rtl.or`, `rtl.xor` | Matching `comb` operation |
| `rtl.add`, `rtl.sub`, `rtl.mul` | Matching `comb` operation |
| `rtl.shl`, `rtl.shru`, `rtl.shrs` | Matching `comb` shift after operand-width normalization |
| `rtl.eq`, `rtl.ult`, `rtl.slt` | `comb.icmp` with `eq`, `ult`, or `slt` predicate |
| `rtl.concat` | `comb.concat` |
| `rtl.extract`, `rtl.trunc` | `comb.extract` |
| `rtl.zext`, `rtl.sext` | Zero/sign materialization plus `comb.concat` |

CIRCT shifts require equal-width operands, while Rhodium permits an
independently sized amount. A narrower amount is zero-extended to the value
width. With a wider amount, the value is widened, shifted at the amount width,
and truncated to its declared result width; signed right-shift values are
sign-extended and other values are zero-extended. This retains Rhodium's
fixed-width overflow and overshift behavior.

`sv.constantX` is a synthesis-freedom carrier, not four-state Rhodium value
semantics. Downstream RTL simulation can display or propagate X, but the public
Rhodium operations continue to use the ordinary two-state hardware model.

### Selection and relations

| Rhodium IR | CIRCT lowering |
|---|---|
| `rtl.mux_lookup` | Key comparisons and a `comb.mux` tree; a one-bit key-1 case is one `comb.mux` |
| `rtl.onehot_mux` | Selector-bit gating and a balanced `comb.or` tree |
| `rtl.decode` | `sv.alwayscomb` containing one sparse `sv.case casez` |
| `rtl.vector_write_set` | Symmetric per-element decode, gated data, balanced OR merge, and old-element fallback |

A one-hot mux intentionally adds no validity detector: zero-hot and multi-hot
selectors are outside its result contract. Choices are packed when necessary,
gated by their selector bits, OR-reduced, and cast back to the result type.

Vector write sets compare every enabled port with every destination element.
The lowering OR-merges same-element data and keeps the old element when no port
matches. It has neither priority nor collision detection; enabled writes to the
same index are outside the operation's contract.

Decode relations stay relational until this backend. Input-care masks become
`z` wildcard positions in `casez`; partially specified outputs use
`sv.constantX` for uncared bits. Core verification establishes non-overlapping
rows, so source row order is irrelevant. CIRCT and downstream synthesis choose
the resulting gate implementation; Rhodium runs no backend-side minimizer.

### Aggregates

| Rhodium IR | CIRCT lowering |
|---|---|
| `rtl.record_create`, `rtl.record_get` | `hw.struct_create`, `hw.struct_extract` |
| `rtl.vector_create` | `hw.array_create`, reversing operands to preserve Rhodium element numbering |
| `rtl.vector_get` | `hw.array_get` with a host-static index |
| `rtl.vector_index`, `rtl.vector_inject` | Dynamic `hw.array_get`, `hw.array_inject` |

### Storage

| Rhodium resource | CIRCT lowering |
|---|---|
| `rtl.memory` | `seq.hlmem` |
| `rtl.memory_read_async` | Latency-zero `seq.read` |
| `rtl.memory_write` | Latency-one `seq.write` |
| `rtl.sync_memory` | `seq.firmem` with native read, write, or shared read-write ports |

The older asynchronous-read `Memory` resource stays on `seq.hlmem`.
Synchronous-memory elements are packed to the integer width required by
`seq.firmem` and bitcast back at aggregate port boundaries. A declared mask
granularity determines the FIR memory's mask width, and write and shared
read-write ports pass that mask directly. The CIRCT generated-memory flow then
preserves the declared port topology, enables, and packed-lane masks while
producing its simulation SystemVerilog module.

### Verification, CDC evidence, and simulation effects

| Rhodium IR | CIRCT lowering |
|---|---|
| `cdc.sync_level` | No emitted operation; verified stage registers receive `async_reg = "TRUE"` SV attributes |
| `verif.assert` | Reset-suppressed, guard-enabled, rising-edge `verif.clocked_assert` |
| `sim.dpi_call` | Enabled, clocked, result-less `sim.func.dpi.call` |
| `sim.dpi_register` | Enabled, clocked, result-bearing `sim.func.dpi.call` |

`cdc.sync_level` is durable analysis evidence rather than hardware. Core
verification proves that it identifies a resetless, direct, one-bit register
chain on one destination clock. The backend omits the evidence operation and
marks only those verified stage registers.

For `verif.assert`, the backend ANDs the activation guard with the inverse of
active-high reset and uses that value as CIRCT's assertion enable. The external
test pipeline lowers the operation to a SystemVerilog concurrent assertion,
preserves its optional label, places non-synthesizable output behind a
`SYNTHESIS` guard, and runs Verilator with assertion evaluation enabled.

DPI imports become module-level `sim.func.dpi` declarations. Both DPI operation
forms convert the Rhodium clock with `seq.to_clock`, pass the explicit enable,
and differ only in whether the CIRCT call returns SSA results.

## Validation ownership

Contributor ownership and the backend change workflow are in
[`DEVELOPING.md`](DEVELOPING.md#validation). Commands and fixture selection are
in the [backend test guide](../../tests/backend/README.md); exact-reference and
fixture maintenance are in its
[`DEVELOPING.md`](../../tests/backend/DEVELOPING.md).
