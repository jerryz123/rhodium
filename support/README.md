<!-- Guides dependency-neutral host refinements and immutable hardware-data descriptions. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Shared host descriptions

`support/` is a repository-level leaf package for small host-value refinements
and immutable data descriptions that unrelated packages can import without
acquiring Rhodium, protocol, ISA, or implementation dependencies. Start with
[`annotations.rhm`](annotations.rhm) for host constraints or
[`hardware-enum.rhm`](hardware-enum.rhm) for enums shared by host code and RTL.
The authoritative repository package boundary is in
[`rhodium/DEVELOPING.md`](../rhodium/DEVELOPING.md#package-responsibilities).

This directory is not [`rhodium/frontend/support/`](../rhodium/frontend/support/).
That directory implements shared frontend macro and static-information
mechanisms and may depend on the frontend kernel, core APIs, and analyses.
Contributors changing these refinements should read
[`DEVELOPING.md`](DEVELOPING.md).

## Decide where a refinement belongs

Put a refinement here when it expresses a context-free constraint on an
ordinary host value, has vocabulary useful outside one domain, and can remain
implemented entirely with Rhombus. A caller should be able to adopt it without
depending on the package that first needed it.

Otherwise, keep the rule with its semantic owner. For example, power-of-two
integers belong with the bit utilities in
[`rhodium/std/bits.rhdl`](../rhodium/std/bits.rhdl), while CHI field-width rules
belong in [`chi/protocol/params.rhdl`](../chi/protocol/params.rhdl). Use built-in annotations such
as `NonemptyList.of(T)`, `Nat`, and `PosInt` directly instead of adding aliases
here. Relationships among values, uniqueness, ownership, reserved names,
protocol legality, and IR validity remain explicit checks in the package that
owns those semantics.

## Public annotations

| Annotation | Accepted value | Current consumers |
| --- | --- | --- |
| `NonemptyString` | Any `String` with at least one character; it does not imply identifier syntax | NoC symbolic names and routing policy; RISC-V fields, formats, instruction catalogs, and ISA helpers; CHI, device, and SoC parameter names |
| `AsciiIdentifier` | A `String` matching `[A-Za-z_][A-Za-z0-9_]*` | Rhodium core and frontend APIs that name modules, ports, fields, values, memories, instances, registers, and related generated hardware |

Both are annotation macros, so use them directly at an API boundary:

```rhombus
fun named_region(name :: NonemptyString):
  // ...
```

Choose `AsciiIdentifier` only when downstream hardware or generated-symbol
syntax requires that narrower spelling. Choose `NonemptyString` when punctuation
or other nonempty text remains meaningful to the owning domain.

## Enums shared by host code and RTL

Import [`hardware-enum.rhm`](hardware-enum.rhm) from ordinary Rhombus:

```rhombus
import lib("support/hardware-enum.rhm").hardware_enum

hardware_enum Command:
  Idle = 0
  Load = 8

Command.Load.packed_value() // 8, without elaboration
Command.values()            // declaration-order member constants
```

`#lang rhodium` exposes the same declaration. Import the declared `Command`
directly into RTL and use it in ports, bundles, registers, comparisons, or mux
keys; no second declaration or conversion is required. Ordinary host annotations
accept only constants from that enum. In a hardware authoring context, the same
annotation also accepts signals of that exact type.

Automatic, explicit-value, explicit-width, and `~encoding: one_hot` forms share
the same descriptors. Members have nominal host equality and hashing: separately
declared enums remain distinct even with identical names and encodings.
Duplicate encodings and insufficient widths are rejected. Hardware-owned methods
are supplied by the Rhodium authoring layer; see its
[enum contract](../rhodium/frontend/layers/README.md#hardware-enums).

## Boundaries and non-goals

This package defines host data, not live hardware, elaboration, protocol
legality, IR checks, or backend behavior. Contributor placement and dependency
rules are in [`DEVELOPING.md`](DEVELOPING.md#boundaries-and-placement).
