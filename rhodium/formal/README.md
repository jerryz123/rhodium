<!-- Documents the optional Rosette formal engine, its supported semantics, API, and focused validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rhodium formal engine

The formal engine is an optional, solver-backed consumer of verified public
Rhodium IR. It checks deterministic combinational behavior with Rosette
fixed-width bitvectors; it does not participate in elaboration, change Rhodium
semantics, or get imported by `#lang rhodium`.

This README documents the implemented public contract. The
[formal plan](PLAN.md) sets out Rhodium's direction: executable specifications,
leaf refinement proofs, and parent proofs that use verified child models.
Contributors changing solver semantics, query handling, replay, or coverage
should read [`DEVELOPING.md`](DEVELOPING.md).

## Install and quick start

Install Rosette into the active Racket installation:

```sh
raco pkg install --auto rosette
```

Run the focused formal suite. It first probes Rosette and its solver under the
persistent isolated build cache, then runs the Rhodium formal tests:

```sh
moon run rhodium:formal-test
```

Rosette is optional: ordinary Rhodium host and backend targets do not require
it. When CIRCT and Verilator are also available, run the independent backend
differential check:

```sh
moon run rhodium:formal-differential-test
```

## Formal API

Import `rhodium/formal/main.rhm` explicitly. Each subject must be a verified
`DesignElaboration` with an explicit top or a completed core `Module` owned by
its verified design. A raw `Design` is rejected because it does not identify a
top module.

Use `check_equivalent(left, right, query = FormalQuery())` to compare every
paired top output on every permitted input. It returns a `FormalResult`.

Equivalence requires exactly the same top input and output names and mutually
equal Rhodium types. Paired inputs receive the same symbolic value, and all
outputs are compared simultaneously. Module structure, internal names,
operation order, and printed IR may differ; this is behavioral equivalence,
not a structural diff.

### Assumptions and packed patterns

`FormalQuery` contains a conjunction of explicit assumptions over named top
inputs:

| Form | Meaning |
|---|---|
| `FormalInputAssumption(port, value)` | The complete canonical packed input equals `value` |
| `FormalInputAssumption(port, value, care)` | The cared bits satisfy `input & care == value` |
| `FormalOneHotAssumption(port)` | The complete packed input has exactly one set bit |

Values and masks must fit the named port's canonical packed width, and
`value & care` must equal `value`. Omitting `care` selects the all-ones mask.
Aggregate ports use their canonical Rhodium packed representation.
No symbolic Rosette value or live
Rhodium object crosses the query boundary.

For example, this query fixes `mode`, constrains the high bits of `tag` to
`10`, and requires `grant` to be exactly one-hot:

```rhombus
def query:
  FormalQuery([FormalInputAssumption("mode", 0b0000),
               FormalInputAssumption("tag", 0b1000, 0b1100),
               FormalOneHotAssumption("grant")])
def equivalent = check_equivalent(reference, candidate, query)
```

Every reachable `rtl.onehot_mux`, including one below an instance or driven by
a derived value, creates a separate validity obligation. The engine proves
from the complete query that its selector is exactly one-hot before evaluating
the requested query. A missing or insufficient proof returns `unsupported`
with the operation path. Contradictory assumptions return `vacuous` before any
one-hot validity claim.

Every reachable `rtl.urem` likewise requires a proof that its divisor is nonzero,
including unused operations and operations beneath instances. The engine returns
`unsupported` if the query does not establish that precondition; it does not use
SMT's chosen zero-divisor value as an RTL guarantee.

### Result statuses

An `equivalent` result means no permitted input produces a difference. A
`counterexample` retains the assumptions, complete input assignment and all
differing outputs. Contradictory assumptions yield `vacuous`; an incompatible
interface or unsupported operation yields `unsupported`; solver uncertainty yields
`unknown`. None of the latter three is a proof.

Inputs omitted from a partial solver model are completed with zero. The engine
then interprets the design concretely before returning a counterexample, so
reported packed values are replayable nonnegative integers.
`unsupported` includes a focused diagnostic for the first rejected reachable
operation when applicable; engine bugs are not converted into scope results.
Invalid API arguments, unknown assumption ports, out-of-width packed
patterns, and raw `Design` subjects raise errors instead of returning a formal
status.

## Supported combinational semantics

The engine validates the complete module hierarchy reachable from the selected
top, not only the backward slice of a requested output. Every value must be a
positive-width flat, record, or vector data type with a canonical packed
layout. A record's first declared field occupies the most-significant packed
bits; vector element zero occupies the least-significant element-width bits.
Frontend-defined flat types need no solver special case. Strict interface
checks still use Rhodium type equality rather than packed width alone.

The implemented deterministic operation contract is:

| Category | Supported operations |
| --- | --- |
| Structure | `rtl.input_port`, `rtl.output_port`, `rtl.wire`, `rtl.drive`, `rtl.instance` |
| Constants and bitwise | `rtl.constant`, `rtl.not`, `rtl.and`, `rtl.or`, `rtl.xor`, `rtl.any`, `rtl.all` |
| Modular arithmetic | `rtl.add`, `rtl.mul`, `rtl.sub` |
| Unsigned remainder | `rtl.urem` when query assumptions prove the divisor nonzero |
| Shifts | `rtl.shl`, `rtl.shru`, `rtl.shrs`, including unequal operand widths and overshifts |
| Comparisons | `rtl.eq`, `rtl.ult`, `rtl.slt` |
| Selection | total `rtl.mux_lookup`; non-overlapping `rtl.decode` with every output bit cared in every case and the default; `rtl.onehot_mux` when query assumptions prove its selector exactly one-hot |
| Packing and widths | `rtl.cast`, `rtl.concat`, `rtl.extract`, `rtl.zext`, `rtl.sext`, `rtl.trunc` |
| Aggregates | `rtl.record_create`, `rtl.record_get`, `rtl.vector_create`, `rtl.vector_get` |

Shifts include Rhodium's unequal value/amount-width normalization and defined
overshift behavior. Hierarchy is interpreted compositionally through driven
instance ports. Equal-width casts preserve the packed representation.

## Scope limits and fail-closed behavior

The equivalence query never invents values or assumptions for unspecified behavior.
Structural exclusions are rejected during preflight. When assumptions are
present, their feasibility is checked first; operation validity is then proved
before the requested query.

| Unsupported category | Current stop condition |
|---|---|
| Free or partial values | `rtl.dont_care`; any `rtl.decode` row or default with an uncared output bit |
| Unproved partial selection | Any `rtl.onehot_mux` whose exactly-one selector condition is not implied by the query |
| Unproved remainder domain | Any `rtl.urem` whose nonzero-divisor condition is not implied by the query |
| Sequential behavior | Registers, reset behavior, every memory form, and memory writes |
| Verification effects | Assertions, including `verif.assert` |
| External behavior | DPI calls and DPI result registers |
| Types | `Clock`, `Reset`, nonpackable values, and types without a positive canonical packed width |
| Schema coverage | Any operation not listed in the supported-semantics table |

An internal single-clock stepper also executes register-only hierarchies for
concrete replay and symbolic next-state calculations. It is not a public
sequential proof or refinement API. See the [transition boundary](DEVELOPING.md#register-transition-foundation).

This is not an unbounded or bounded sequential model checker, a four-state
simulator, a structural equivalence checker, or a synthesis engine. It does
not assign semantics to invalid one-hot selectors, memory collisions,
disabled memory reads, external DPI behavior, or other unspecified hardware
choices.

## Validation targets

| Command | Coverage |
|---|---|
| `moon run rhodium:formal-test` | Focused public-API, assumption, operation, hierarchy, aggregate, decode, register stepping, replay, and fail-closed tests after a live Rosette/Z3 probe |
| `moon run rhodium:formal-differential-test` | Independent CIRCT/Verilator replay for structurally different shifts, aggregates, hierarchy, fully cared decode, and assumption-constrained one-hot implementations, plus register traces against native and HDL simulations |

Detailed test ownership and the differential oracle matrix are in
[`DEVELOPING.md`](DEVELOPING.md#validation). Set
`CIRCT_OPT=/path/to/circt-opt` when the pinned tool is not under the current
checkout's `.tools/` directory.

## Compatibility and troubleshooting

| Component | Supported or validated evidence |
|---|---|
| Racket | Rosette declares Racket 8.1 or newer; this repository's formal target is validated with Racket 9.2 |
| Rosette | The focused targets require and are validated with Rosette 4.0 |
| Solver | The focused targets require and are validated with Rosette's pinned Z3 4.8.8 binary |

Other compatible Racket releases have not been exercised by this target.

If `moon run rhodium:formal-test` fails before running tests, first run the install command
again in the same Racket installation. The target deliberately performs a live
solver probe, so a successful package import without a working solver is still
a setup failure. The repository does not vendor Rosette or its transitive
packages.

<details>
<summary>Historical clean-install evidence</summary>

A clean catalog installation from an empty `PLTUSERHOME` was validated with
Rosette checksum `373c8c35e4a7667f38fce10cf0b74ae17de07f1d` and transitive
`rfc6455` checksum `e3a87e914e25841a6e1bb996aa001aeb178284bf`.
An earlier `rfc6455` resolution failure was caused by a restricted test
environment, not an unavailable package source.

</details>
