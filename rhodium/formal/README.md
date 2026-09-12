<!-- Documents the optional Rosette formal engine, its supported semantics, API, and focused validation. -->

# Rhodium formal engine

The formal engine is an optional, solver-backed consumer of verified public
Rhodium IR. It checks deterministic combinational behavior with Rosette
fixed-width bitvectors; it does not participate in elaboration, change Rhodium
semantics, or get imported by `#lang rhodium`.

| Current query | Question |
|---|---|
| Equivalence | Do two strict-interface-compatible tops produce the same outputs for every permitted input? |
| Reachability | Does some permitted input make one top output match a packed pattern? |
| Output property | Does one top output match a packed pattern for every permitted input? |

Implementation status and future stages are owned by the
[`formal-engine plan`](PLAN.md). This README documents only the implemented
public contract.
Contributors changing solver semantics, query handling, replay, or coverage
should read [`DEVELOPING.md`](DEVELOPING.md).

## Install and quick start

Install Rosette into the active Racket installation:

```sh
raco pkg install --auto rosette
```

Run the focused formal suite. It first probes Rosette and its solver under an
isolated compiled root, then runs the Rhodium formal tests:

```sh
make formal-test
```

Rosette is optional: ordinary Rhodium host and backend targets do not require
it. When CIRCT and Verilator are also available, run the independent backend
differential check:

```sh
make formal-differential-test
```

## Formal API

Import `rhodium/formal/main.rhm` explicitly. Each subject must be a verified
`DesignElaboration` with an explicit top or a completed core `Module` owned by
its verified design. A raw `Design` is rejected because it does not identify a
top module.

| Function and result | Contract | Successful result |
|---|---|---|
| `check_equivalent(left, right, query = FormalQuery())` → `FormalResult` | Compare all paired top outputs for every input allowed by `query` | `equivalent` |
| `check_reachable(subject, target, query = FormalQuery())` → `FormalReachabilityResult` | Find an input for which one named top output matches `target` | `reachable` with assignments and the complete output value |
| `check_output_property(subject, target, query = FormalQuery())` → `FormalPropertyResult` | Check that one named top output always matches `target` | `proved` |

Equivalence requires exactly the same top input and output names and mutually
equal Rhodium types. Paired inputs receive the same symbolic value, and all
outputs are compared simultaneously. Module structure, internal names,
operation order, and printed IR may differ; this is behavioral equivalence,
not a structural diff.

`FormalOutputPattern(port, value, care = #false)` names one top output.
`check_reachable` treats it existentially; `check_output_property` treats it
universally. The latter is a combinational output query and does not
reinterpret clocked `verif.assert` operations.

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
The same validation applies to `FormalOutputPattern`. Aggregate ports use their
canonical Rhodium packed representation. No symbolic Rosette value or live
Rhodium object crosses the query boundary.

For example, this query fixes `mode`, constrains the high bits of `tag` to
`10`, and requires `grant` to be exactly one-hot:

```rhombus
def query:
  FormalQuery([FormalInputAssumption("mode", 0b0000),
               FormalInputAssumption("tag", 0b1000, 0b1100),
               FormalOneHotAssumption("grant")])
def equivalent = check_equivalent(reference, candidate, query)
def reachable:
  check_reachable(candidate,
                  FormalOutputPattern("response", 0b1000, 0b1100),
                  query)
def property:
  check_output_property(candidate,
                        FormalOutputPattern("response", 0b1000, 0b1100),
                        query)
```

Every reachable `rtl.onehot_mux`, including one below an instance or driven by
a derived value, creates a separate validity obligation. The engine proves
from the complete query that its selector is exactly one-hot before evaluating
the requested query. A missing or insufficient proof returns `unsupported`
with the operation path. Contradictory assumptions return `vacuous` before any
one-hot validity claim.

### Result statuses

| Status | Query | Meaning |
|---|---|---|
| `equivalent` | Equivalence | No permitted input makes a paired output differ |
| `reachable` | Reachability | A replayed assignment reaches the requested output pattern |
| `unreachable` | Reachability | No permitted input reaches the requested output pattern |
| `proved` | Output property | Every permitted input satisfies the requested output pattern |
| `counterexample` | Equivalence or output property | A replayed assignment demonstrates a difference or property violation |
| `vacuous` | All | The explicit assumptions are mutually unsatisfiable; no success claim is made |
| `unsupported` | All | An equivalence interface mismatch, or a reachable type, operation, or semantic case, is outside the implemented contract |
| `unknown` | All | Rosette could not decide assumption feasibility, a one-hot obligation, or the requested query |

Equivalence counterexamples contain every top input assignment and every
differing output. Reachability witnesses and property counterexamples contain
every top input assignment and the complete concrete target output, even when
only some output bits were cared. All retain the query assumptions.

Inputs omitted from a partial solver model are completed with zero. The engine
then interprets the design concretely before returning any counterexample or
witness, so reported packed values are replayable nonnegative integers.
`unsupported` includes a focused diagnostic for the first rejected reachable
operation when applicable; engine bugs are not converted into scope results.
Invalid API arguments, unknown target or assumption ports, out-of-width packed
patterns, and raw `Design` subjects raise errors instead of returning a formal
status.

## Supported semantics

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
| Constants and bitwise | `rtl.constant`, `rtl.not`, `rtl.and`, `rtl.or`, `rtl.xor` |
| Modular arithmetic | `rtl.add`, `rtl.mul`, `rtl.sub` |
| Shifts | `rtl.shl`, `rtl.shru`, `rtl.shrs`, including unequal operand widths and overshifts |
| Comparisons | `rtl.eq`, `rtl.ult`, `rtl.slt` |
| Selection | total `rtl.mux_lookup`; non-overlapping `rtl.decode` with every output bit cared in every case and the default; `rtl.onehot_mux` when query assumptions prove its selector exactly one-hot |
| Packing and widths | `rtl.cast`, `rtl.concat`, `rtl.extract`, `rtl.zext`, `rtl.sext`, `rtl.trunc` |
| Aggregates | `rtl.record_create`, `rtl.record_get`, `rtl.vector_create`, `rtl.vector_get`, `rtl.vector_write_set` |

Shifts include Rhodium's unequal value/amount-width normalization and defined
overshift behavior. Hierarchy is interpreted compositionally through driven
instance ports. Equal-width casts preserve the packed representation.

## Scope limits and fail-closed behavior

The engine never invents values or assumptions for unspecified behavior.
Structural exclusions are rejected during preflight. When assumptions are
present, their feasibility is checked first; one-hot validity is then proved
before the requested query.

| Unsupported category | Current stop condition |
|---|---|
| Free or partial values | `rtl.dont_care`; any `rtl.decode` row or default with an uncared output bit |
| Unproved partial selection | Any `rtl.onehot_mux` whose exactly-one selector condition is not implied by the query |
| Sequential behavior | Registers, reset behavior, every memory form, and memory writes |
| Verification effects | Assertions, including `verif.assert` |
| External behavior | DPI calls and DPI result registers |
| Types | `Clock`, `Reset`, nonpackable values, and types without a positive canonical packed width |
| Schema coverage | Any operation not listed in the supported-semantics table |

This is not an unbounded or bounded sequential model checker, a four-state
simulator, a structural equivalence checker, or a synthesis engine. It does
not assign semantics to invalid one-hot selectors, memory collisions,
disabled memory reads, external DPI behavior, or other unspecified hardware
choices. See [`PLAN.md`](PLAN.md) for staged work beyond the implemented
combinational contract.

## Validation targets

| Command | Coverage |
|---|---|
| `make formal-test` | Focused public-API, assumption, operation, hierarchy, aggregate, decode, replay, reachability, property, and fail-closed tests after a live Rosette/Z3 probe |
| `make formal-differential-test` | Independent CIRCT/Verilator replay for structurally different shifts, aggregates, hierarchy, fully cared decode, and assumption-constrained one-hot implementations, including defect counterexamples and output-query witnesses |

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

If `make formal-test` fails before running tests, first run the install command
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

Enabled vector writes are supported when the query assumptions prove every
enabled index is in range and enabled indices are pairwise distinct. Disabled
ports impose no index restriction. Functional `.updated()` supplies its own
range enable, so its out-of-range no-op cases remain provable without additional
input assumptions.
