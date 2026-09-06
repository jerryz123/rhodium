<!-- Guides contributors through extending and validating RV5Stage structured decode. -->

# Developing RV5Stage decode

Read the decode [README](README.md) for specialization, row composition, and
care-mask semantics. This guide owns control-column placement, instruction
extension, source navigation, and focused validation.

## Architecture and dependency boundary

Each component file owns its decoder-facing bundle and every case that
populates that column. Component files do not import sibling control modules;
[`core-ctrl.rhdl`](core-ctrl.rhdl) is the only composition boundary. The parent
[`cores/check-boundaries.sh`](../../check-boundaries.sh) enforces this split.

Decode consumes canonical instruction descriptions from the pure RISC-V model
and converts them through the public RISC-V/Rhodium adapter. It must not create
a parallel instruction-kind enum, duplicate encodings by name, or own pipeline
execution and retirement behavior.

## Control-column ownership

| File | Owned control |
|---|---|
| [`alu-ctrl.rhdl`](alu-ctrl.rhdl) | ALU result selection and modifiers for base integer, B, Zicond, address-generation, and unused-result cases |
| [`operand-ctrl.rhdl`](operand-ctrl.rhdl) | Integer register use, ALU operand routing, and immediate format |
| [`branch-ctrl.rhdl`](branch-ctrl.rhdl) | Branch-resolver mode and JALR target selection |
| [`mem-ctrl.rhdl`](mem-ctrl.rhdl) | Load, store, LR/SC, and AMO operation, width, atomic operation, and load extension |
| [`multiply-ctrl.rhdl`](multiply-ctrl.rhdl) | Multiplier signedness plus high-result and word-result selection |
| [`divide-ctrl.rhdl`](divide-ctrl.rhdl) | Divider signedness plus quotient/remainder and word-result selection |
| [`writeback-ctrl.rhdl`](writeback-ctrl.rhdl) | Scalar architectural write enable and result source |
| [`system-ctrl.rhdl`](system-ctrl.rhdl) | Zicsr operation and ECALL, EBREAK, WFI, MRET, and SRET actions |
| [`fence-ctrl.rhdl`](fence-ctrl.rhdl) | FENCE, FENCE.I, and SFENCE.VMA actions |
| [`fp-ctrl.rhdl`](fp-ctrl.rhdl) | FP register-bank use, destination bank, execution unit, precisions, rounding-mode use, and operation modifiers |
| [`decode-support.rhdl`](decode-support.rhdl) | Catalog-independent case construction, exclusion, exact-pattern comparison, and component lookup helpers |
| [`core-ctrl.rhdl`](core-ctrl.rhdl) | `RV5StageControl`, core-row composition, scalar controls for FP rows, profile validation, and the integrated decoder circuit |

ALU and operand relations use each XLEN catalog's own immediate-shift
`InstructionSpec` because those encodings differ. Other shared instructions
reuse the same model objects; rows match exact input patterns rather than
rebinding by name. Memory width is the shared
[`MemoryWidth`](../../load-store.rhdl) datapath contract.

## Add an instruction

1. Add or select the canonical instruction in the owning RISC-V ISA catalog.
2. Update every complete component relation used by `compose_control_cases`.
   Exact-one lookup deliberately turns a missing or duplicate row into an
   elaboration error.
3. For FP, supply operand metadata and an execution case, include the
   instruction in the selected FP catalog, and let the core adapter own scalar
   columns.
4. Leave inactive fields as explicit care-mask don't-cares only when downstream
   logic cannot observe them behind the corresponding validity or enable bit.
5. Add focused domain and representative-control assertions, then verify the
   integrated bundle contains one `rtl.decode` for each enabled profile.
6. Update [README.md](README.md) when the public specialization matrix or
   control semantics change.

## Implementation map

| Need | Start here |
|---|---|
| Integrated bundle, selected core catalogs, and final decoder | [`core-ctrl.rhdl`](core-ctrl.rhdl) |
| F/D/Zfhmin/Zfh catalogs and FP register/execution controls | [`fp-ctrl.rhdl`](fp-ctrl.rhdl) |
| Shared instruction-pattern conversion | [`../../../riscv/rtl/instruction-pattern.rhdl`](../../../riscv/rtl/instruction-pattern.rhdl) |
| ISA catalogs and profile enums | [`../../../riscv/isa/`](../../../riscv/isa/) |
| Pipeline use of decoded controls | [`../core.rhdl`](../core.rhdl) |
| Integer domain, columns, masks, and bundle shape | [`../tests/core-ctrl-test.rhm`](../tests/core-ctrl-test.rhm) |
| Zicbop override, controls, and core event boundary | [`../tests/zicbop-test.rhm`](../tests/zicbop-test.rhm) |
| FP domains, metadata, profiles, and single-decode structure | [`../tests/fp-ctrl-test.rhm`](../tests/fp-ctrl-test.rhm) |

## Focused validation

From the repository root, run both decode owners in one fresh-root wrapper:

```sh
tools/run-racket-tests.sh \
  cores/rv5stage/tests/core-ctrl-test.rhm \
  cores/rv5stage/tests/fp-ctrl-test.rhm
```

The core-control test checks exact RV32/RV64 domains, representative columns,
nested care masks, bundle shape, and design verification. The FP-control test
checks F/D and optional Zfhmin/Zfh composition, metadata-derived register
controls, representative execution controls, supported profiles, and the
single-decoder invariant. Use the parent
[`DEVELOPING.md`](../DEVELOPING.md#focused-validation) for pipeline and backend
coverage.
