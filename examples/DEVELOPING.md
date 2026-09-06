<!-- Explains how to add, catalog, validate, and maintain Rhodium and RFPL examples. -->

# Developing Rhodium examples

Read the [example guide](README.md) for the executable learning paths and
authoritative example catalog. This guide is for contributors adding examples,
changing their ownership, or maintaining generated references.

## Choose an owner

An example should demonstrate one public contract through the narrowest
realistic program:

- Put language and RTL examples in `rtl/`, clock-analysis examples in
  `clocking/`, and standard-library examples in `std/`.
- Keep pure-model and domain-library examples in their existing `noc/`,
  `riscv/`, `chi/`, `cores/`, and `rv5stage/` groups.
- Use `lop/` only when comparing authoring surfaces that construct equivalent
  public IR.
- Keep RFPL annotations beside their logical design in `rfpl/`; keep optional
  solver-backed examples in `formal/`.

Do not use an example as the only specification of a feature. The owning
package README defines the public contract; the example demonstrates it.

## Add or change an example

1. Add the smallest valid program that demonstrates the intended composition.
2. Give every modified documentation or source file its required purpose
   comment and keep the example import path public.
3. Add the example to the matching catalog section in [README.md](README.md)
   and to the owning example target in the root [`Makefile`](../Makefile).
4. If the program exports a concrete design, add or update its manifest entry
   in [`../tests/backend/run-circt.sh`](../tests/backend/run-circt.sh). Generic
   circuit generators need a concrete elaboration before they can own one exact
   reference.
5. Run the owning example group before the complete non-formal catalog.

Keep valid authoring programs here. Intentional frontend failures belong under
[`../tests/frontend/invalid/`](../tests/frontend/invalid/), and backend-only
integration shapes belong in [`../tests/backend/`](../tests/backend/DEVELOPING.md).

## Maintain generated Verilog

Concrete example designs colocate their exact generated-Verilog reference with
the authoring source so a reviewer can see both sides of an intentional output
change. The backend manifest owns export names, CIRCT grouping, optional
Verilator tops, and reference eligibility.

Do not hand-edit a reference to hide an unexplained diff. Follow the
[backend reference workflow](../tests/backend/DEVELOPING.md#verilog-references),
use the pinned CIRCT version, update only the affected fixture, and review the
example-source diff. Generated MLIR, temporary SystemVerilog, Verilator build
trees, and logs remain untracked.

## Validate a change

Run the narrow group named in the [example guide](README.md#run-the-examples).
Useful maintenance checks are:

```sh
make check-example-verilog
make examples
```

`make check-example-verilog` checks reference exports and backend-manifest
coverage without invoking CIRCT. `make examples` runs every non-formal example;
the Rosette-backed group remains separate as `make examples-formal`.

When author-visible compiler behavior changes, run its owning frontend or core
test. When emitted hardware behavior changes, run the smallest applicable
backend simulation; an example does not need a separate host test merely to
prove that it elaborates. The [test guide](../tests/README.md) explains how to
select that depth.
