<!-- Guides contributors through maintaining the Rhodium Berkeley HardFloat port. -->

# Developing HardFloat for Rhodium

Read the package [README](README.md) for public types, supported formats,
arithmetic components, protocols, provenance, and limits. This guide owns the
port's source map, translation discipline, validation coverage, and maintenance
work.

## Architecture and package boundary

Production files depend only on the public `#lang rhodium` authoring surface,
`rhodium/std/bits.rhdl`, and other files in this package. They must not import
Rhodium implementation layers, backends, tests, processor cores, or RISC-V
definitions. Backend tooling is a test-only consumer. The package-local
[`check-boundaries.sh`](check-boundaries.sh) enforces this boundary.

HardFloat owns floating-point representation and arithmetic. RISC-V NaN-boxing,
rounding-mode selection, CSR policy, register state, and retirement belong in
[`riscv/rtl`](../riscv/rtl/README.md#floating-point-policy) or a concrete core.

## Implementation map

| Path | Responsibility |
|---|---|
| [`main.rhdl`](main.rhdl) | Complete supported public facade |
| [`rtl/types.rhdl`](rtl/types.rhdl) | Format parameters, nominal representations, modes, flags, and raw records |
| [`rtl/recode.rhdl`](rtl/recode.rhdl) | IEEE, recoded, and raw representation conversion |
| [`rtl/resize.rhdl`](rtl/resize.rhdl) | Raw exponent/significand resizing and sticky preservation |
| [`rtl/round.rhdl`](rtl/round.rhdl) | Raw-to-recoded rounding and exception generation |
| [`rtl/primitives.rhdl`](rtl/primitives.rhdl) | HardFloat variable-mask primitive |
| [`rtl/conversion/`](rtl/conversion/) | Integer normalization and integer/RecFN/format conversion |
| [`rtl/conversion/recfn-to-integer-modulo.rhdl`](rtl/conversion/recfn-to-integer-modulo.rhdl) | Rounded power-of-two modulo conversion with ordinary integer-conversion flags |
| [`rtl/classify.rhdl`](rtl/classify.rhdl) | Ten-way recoded classification |
| [`rtl/arithmetic/compare.rhdl`](rtl/arithmetic/compare.rhdl) | Ordered comparison and invalid-operation flagging |
| [`rtl/arithmetic/min-max.rhdl`](rtl/arithmetic/min-max.rhdl) | IEEE minimum, maximum, minimumNumber, and maximumNumber variants |
| [`rtl/arithmetic/round-to-integral.rhdl`](rtl/arithmetic/round-to-integral.rhdl) | Same-format integral rounding with optional inexact reporting |
| [`rtl/arithmetic/add.rhdl`](rtl/arithmetic/add.rhdl) | Close/far add/subtract paths and rounding |
| [`rtl/arithmetic/multiply.rhdl`](rtl/arithmetic/multiply.rhdl) | Raw product, sticky compression, and rounding |
| [`rtl/arithmetic/multiply-add.rhdl`](rtl/arithmetic/multiply-add.rhdl) | Fused pre-multiply, post-multiply, normalization, and final rounding |
| [`rtl/arithmetic/divide-sqrt.rhdl`](rtl/arithmetic/divide-sqrt.rhdl) | Generic one- or two-bit iterative division/square root |
| [`rtl/arithmetic/divide-sqrt-f64.rhdl`](rtl/arithmetic/divide-sqrt-f64.rhdl) | Pipelined multiply-assisted binary64 forms |
| [`tests/`](tests/) | Host representations and CIRCT/Verilator behavior checks |

## Translation and provenance policy

Every derived Rhodium source names its corresponding upstream Scala source and
the pinned commit. Keep the package-level pin and license notice in
[README.md](README.md) synchronized with those source headers.

Translate Chisel width inference into explicit Rhodium widths. Pure
combinational Scala objects become Rhodium functions; meaningful upstream
module boundaries remain circuits. Packed values, special-case behavior,
exception flags, and public sequential protocols are compatibility goals.
Internal hierarchy, temporary names, and a particular generated-netlist shape
are not.

When updating or adding a component:

1. Identify the exact upstream source and pinned revision before translating.
2. Preserve upstream parameter constraints, representation invariants,
   exception behavior, and handshake timing in the public contract.
3. Reuse shared type, recoding, resizing, rounding, and primitive layers rather
   than copying algorithm fragments.
4. Keep host tests for representations and pure reference calculations. Test
   numeric and sequential behavior in a permanent CIRCT/Verilator fixture;
   avoid duplicating it with internal-operation snapshots.
5. Update [README.md](README.md) when public formats, operations, protocols, or
   deliberate limits change.

## Focused validation

From the repository root, run host representation, type, and boundary checks
with:

```sh
make hardfloat-host-test
```

Run CIRCT lowering, generated-SystemVerilog compilation, and the four permanent
Verilator fixtures with:

```sh
make hardfloat-circt-test
```

Run both slices with:

```sh
make hardfloat-test
```

Repository wrappers manage a fresh `PLTCOMPILEDROOTS` when invoked normally.
Direct Racket or Rhombus commands must use a newly created compiled root and
`racket -y` so stale bytecode cannot mask the current source.

The host slice checks format constraints, nominal packed layouts, and public
specialization. The CIRCT and Verilator slice covers representative IEEE
special values, exhaustive F16
representation round trips, rounding families, classification, comparison,
raw resizing, integer and format conversion, add/subtract, multiply,
single-rounding FMA, both iterative divide/square-root options, and specialized
binary64 admission, multiplier integration, exceptional results, rounding, and
ordered overlapping division completion. The numeric-extension fixture also
checks every binary16 encoding across the four minimum/maximum variants and six
rounding modes, including modulo conversion, signed zero, NaN behavior,
optional inexact reporting, and agreement with ordinary integer conversion for
in-range results.

## Follow-up work

1. Add broader differential vectors from Berkeley TestFloat and SoftFloat.
2. Compare representative area and timing with the pinned upstream Chisel
   implementation without making a particular internal netlist contractual.
