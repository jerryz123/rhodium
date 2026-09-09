<!-- Defines the staged architecture, semantics, and validation plan for an optional Rosette formal engine over verified Rhodium IR. -->

# Rosette formal-engine plan

## Status

Implemented through Stage 2's universal combinational output-property query.
The optional public equivalence API, immutable snapshot boundary, Rosette
engine, replayable counterexamples and reachability witnesses, fully cared
deterministic decode semantics, packed constraints, vacuity detection, and
assumption-proved `rtl.onehot_mux` validity have focused and differential
coverage. Other partial-operation contracts and temporal properties remain
open.

This plan resolves the semantic and package boundaries before implementation.
The first deliverable is deliberately a deterministic combinational
equivalence checker, not a general simulator, model checker, or synthesis
system.

## Decision summary

Rhodium will treat Rosette as an optional solver-backed consumer of the verified
public core IR. Rosette will not participate in frontend elaboration, determine
generator behavior, add symbolic hardware values to ordinary Rhodium programs, or
change the meaning of an existing core operation.

The dependency direction will be:

```text
#lang rhodium or core Builder
            |
            v
    elaborate and verify
            |
            v
      public core IR
       /          \
      v            v
CIRCT backend   Rosette formal engine
```

The formal engine will import core only. Core, frontend, language profiles,
the CIRCT backend, and standard libraries will not import the formal engine.
No generic backend or plugin protocol will be introduced. Shared traversal or
semantic infrastructure will be extracted only after the CIRCT and Rosette
consumers demonstrate a concrete stable common requirement.

## Goals

The staged subsystem should eventually support:

1. Behavioral equivalence of two Rhodium modules under a stated semantic scope.
2. Concrete counterexamples expressed with Rhodium port names and types.
3. Bounded checking of existing `verif.assert` operations.
4. Solver-generated traces for testing and reachability questions.
5. Explicit sketch synthesis only after verification semantics are trusted.

The first milestone covers only goals 1 and 2.

## Non-goals

The initial subsystem will not:

- make symbolic values legal in host conditionals or generator parameters;
- reinterpret synthesis freedom as four-state runtime `X` behavior;
- silently assign semantics to invalid one-hot selections, memory collisions,
  disabled memory reads, or external DPI behavior;
- prove unbounded sequential properties;
- replace CIRCT lowering, Verilator simulation, or existing core verification;
- accept generated Verilog as its primary semantic input;
- synthesize arbitrary RTL or mutate the public IR;
- add a second public hardware IR.

## Package and interoperability boundary

The proposed implementation lives under `rhodium/formal/`:

| File | Responsibility |
|---|---|
| `main.rhm` | Public Rhombus API and Rhodium-native result objects |
| `snapshot.rhm` | Read-only traversal of verified public IR into a plain immutable solver snapshot |
| `engine.rkt` | Rosette bitvector interpretation, query construction, solving, and model extraction |
| `README.md` | Supported semantics, API examples, setup, and diagnostics |

Rhombus and Racket share a module system, but Rhombus class bindings are not a
stable Racket object-inspection API. `snapshot.rhm` will therefore traverse
Rhodium objects through their documented Rhombus properties and methods, then
pass only immutable strings, integers, booleans, hashes, and pair lists across
the language boundary. `engine.rkt` must not inspect Rhombus structure layout
or depend on printed IR.

The repository boundary checker currently reserves `.rkt` under `rhodium/` for
reader shims. Implementing `engine.rkt` requires a narrow documented exception
for `rhodium/formal/engine.rkt`, plus a rule that `rhodium/formal/` may depend on core
and Rosette but not frontend or backend modules.

Rosette remains an optional package dependency. `make host-test` and ordinary
Rhodium use must not require it. A focused `make formal-test` target will require
Rosette and its solver. CI should pin the Rosette package checksum and verify
the solver version before the formal target is made required.

## Public API boundary

The first public entry point will have the conceptual shape:

```text
check_equivalent(left, right, query = FormalQuery()) -> FormalResult
check_reachable(subject, target, query = FormalQuery()) -> FormalReachabilityResult
check_output_property(subject, target, query = FormalQuery()) -> FormalPropertyResult
```

`left` and `right` must each be either:

- a `DesignElaboration`, whose explicit `top` is checked; or
- a completed `Module`, whose owning verified design supplies reachable child
  definitions.

A raw `Design` will not be accepted because it does not identify a top module.
Callers with a frontend circuit should use `elaborate_with_top`; callers using
the core Builder should retain the completed top `Module`.

The first milestone compares the complete top-level interface:

- inputs are paired by exact port name;
- outputs are paired by exact port name;
- both sides must have the same input and output name sets;
- paired ports must have mutually equal Rhodium hardware types, not merely equal
  packed widths;
- every paired input receives the same symbolic value;
- all paired outputs are compared simultaneously.

Explicit port mappings and selected-output checks remain deferred. Assumptions
are explicit packed patterns over named top inputs in a separate `FormalQuery`;
the engine never infers likely renames or validity constraints.

`FormalResult` will distinguish at least:

| Status | Meaning |
|---|---|
| `equivalent` | The mismatch query is unsatisfiable under the supported semantics |
| `counterexample` | A satisfying assignment makes at least one paired output differ |
| `vacuous` | The explicit top-input assumptions are mutually unsatisfiable |
| `unsupported` | The reachable design uses a type, operation, or semantic case outside the implemented contract |
| `unknown` | The solver cannot decide the query within the configured limits |

A counterexample will contain typed top-level input assignments and the left
and right values of every differing output. Unsupported results will name the
operation, module, source location, and origin when available. Solver failures
and implementation bugs must remain distinct from `unsupported`; broad
exception-catching must not disguise engine defects as user-facing scope
limits.

`check_reachable` accepts one `FormalOutputPattern` naming a top output with a
packed value and optional care mask. Its result distinguishes `reachable`,
`unreachable`, `vacuous`, `unsupported`, and `unknown`. A reachable result
contains every typed top-input assignment and the complete concrete target
output value; the witness is replayed before it crosses the public boundary.

`check_output_property` accepts the same `FormalOutputPattern`. Its result
distinguishes `proved`, `counterexample`, `vacuous`, `unsupported`, and
`unknown`. A counterexample contains every typed top-input assignment and the
complete concrete violating output value, replayed before it crosses the public
boundary.

## Milestone 1 semantics: deterministic combinational equivalence

### Preconditions

Both input subjects must:

1. belong to verified designs;
2. identify completed top modules;
3. have compatible strict interfaces;
4. contain no combinational cycles, as guaranteed by core verification; and
5. use only the supported operations and semantic cases in every module
   reachable from the top.

The engine validates the entire reachable module hierarchy, not only the
backward slice of top outputs. This keeps the milestone contract simple and
prevents unsupported verification or simulation effects from being silently
ignored.

### Types

The interpreter represents each packable Rhodium value as one Rosette bitvector
of its canonical packed width.

- A `ScalarDataType` uses its declared bit width.
- Record fields retain declaration order, with the first field occupying the
  most-significant packed bits.
- Vector element zero occupies the least-significant element-width bits.
- Frontend-defined flat types need no solver special case; their Rhodium type
  equality remains relevant at interfaces, while their runtime carrier is a
  bitvector.
- `Clock` and `Reset` are not ordinary milestone-1 data inputs. Encountering
  operations that give them temporal meaning makes the design unsupported.
- A type without a canonical packed width is unsupported.

Equal-width `rtl.cast` is a representation-preserving identity on the packed
bitvector. Record and vector construction and projection must be implemented
from the documented canonical layout, not inferred from CIRCT text.

### Supported operations

Milestone 1a supports the following deterministic operations:

| Group | Operations |
|---|---|
| Structure | input port, output port, wire, drive, instance |
| Sources | constant |
| Bitwise | not, and, or, xor |
| Modular arithmetic | add, sub, multiply |
| Shifts | logical left, unsigned right, signed right |
| Comparison | equality, unsigned less-than, signed less-than |
| Selection | total `rtl.mux_lookup` |
| Conversion | equal-width cast |
| Width operations | concat, extract, zero extension, sign extension, truncation |
| Aggregates | record create/get, vector create/get |

Shift interpretation must reproduce Rhodium's declared overshift behavior and
its unequal value/amount-width normalization. Tests will cover amount widths
both smaller and larger than the shifted value.

Hierarchy is interpreted compositionally. Each instance receives the symbolic
values of its driven input places, evaluates the referenced child definition,
and returns child outputs in declared port order. Instance names and operation
IDs are identities for diagnostics and memoization, not semantics.

Milestone 1b adds `rtl.decode` only for deterministic relations whose every
output bit is cared in every row and in the default. Input cubes may remain
partially cared because they describe deterministic matching. Any free output
bit keeps the operation unsupported until relational nondeterminism is
designed.

### Explicitly unsupported operations and cases

Milestone 1 rejects:

- `rtl.dont_care`;
- `rtl.onehot_mux`, because its invalid-selector behavior is unspecified and
  milestone 1 has no assumptions API (Stage 2 supports it only after a separate
  solver query proves exactly-one validity under explicit assumptions);
- any `rtl.decode` with an uncared output bit;
- registers and reset semantics;
- asynchronous-read and synchronous memories;
- memory writes and all collision or out-of-range behavior;
- `verif.assert`;
- DPI calls and DPI result registers;
- unknown operation schemas;
- nonpackable values.

These are semantic stop conditions, not missing default cases. Each must
produce an `unsupported` result with a focused diagnostic.

## Equivalence query

For paired symbolic inputs `I`, explicit assumptions `A(I)`, left outputs
`L(I)`, and right outputs `R(I)`, the engine first checks `A(I)` for
satisfiability and then asks Rosette to solve:

```text
exists I. A(I) and any paired output L(I) != R(I)
```

Before the mismatch query, every supported partial operation contributes a
validity obligation `V(I)`. The engine solves `A(I) and not V(I)` separately.
Only `UNSAT` discharges the operation precondition; `SAT` produces a
source-aware `unsupported` result, and `UNKNOWN` remains `unknown`. Stage 2's
first such obligation is exactly-one validity for every reachable
`rtl.onehot_mux` selector.

- Unsatisfiable `A(I)` produces `vacuous`, not `equivalent`.
- `UNSAT` produces `equivalent`.
- `SAT` produces `counterexample` and a concrete model for all top inputs and
  differing outputs.
- Solver `UNKNOWN`, timeout, or resource exhaustion produces `unknown`, never
  `equivalent`.

Symbol names must be generated from a query-local namespace so repeated or
parallel checks cannot alias accidentally. The Rosette verification condition
and solver state must be isolated per query.

## Reachability query

For one subject output `O(I)` and packed target pattern `(P, C)`, reachability
uses the same assumption feasibility and partial-operation validity checks as
equivalence, then solves:

```text
exists I. A(I) and (O(I) & C) == P
```

`SAT` produces `reachable` with a concretely replayed witness. `UNSAT` produces
`unreachable`. Unsatisfiable assumptions still produce `vacuous` before the
target is classified, and failure to discharge a partial-operation obligation
remains `unsupported`.

## Universal output-property query

For one subject output `O(I)` and packed target pattern `(P, C)`, the universal
property query uses the same assumption feasibility and partial-operation
validity checks, then searches for a violation:

```text
exists I. A(I) and (O(I) & C) != P
```

`UNSAT` produces `proved`. `SAT` produces `counterexample` with a concretely
replayed violating output. Unsatisfiable assumptions produce `vacuous`, and
validity or solver failures retain their `unsupported` or `unknown`
classifications. This query is deliberately about combinational top outputs;
it does not assign bounded or unbounded semantics to clocked `verif.assert`.

## Synthesis freedom and relational semantics

Rhodium's `rtl.dont_care`, uncared decode bits, invalid one-hot selectors, and
several memory cases denote permitted implementation freedom. They are not
ordinary fresh input values and are not four-state `X` values.

A later milestone must choose and document a relational contract before
supporting them. Candidate questions include:

- whether equivalence means equality of all permitted behaviors or mutual
  refinement of behavior sets;
- which choices are stable across uses, cycles, and compared designs;
- how caller assumptions discharge partial-operation preconditions; and
- whether implementation freedom is existential or universal in each query.

Until these quantifier and sharing rules are settled for a specific operation,
the engine must reject it instead of allocating independent or shared symbolic
holes opportunistically. Exactly-one validity is now settled for
`rtl.onehot_mux`; its invalid domain remains uninterpreted and unreachable in a
successful equivalence query.

## Validation strategy

The Rosette interpreter becomes part of the trusted computing base. Validation
must therefore test semantic behavior, not just solver status strings.

### Operation-level tests

For every supported opcode:

- exhaustively compare concrete Rosette evaluation against a small-width
  independent oracle;
- cover boundary values, overflow, signed interpretation, extraction bounds,
  and overshifts;
- cover records and vectors with asymmetric field or element values so layout
  reversals are visible;
- include one deliberately incorrect expected relation to ensure a concrete
  counterexample is produced.

Small exhaustive tests should normally use widths of one through four bits.
Wider focused cases should cover behaviors that depend on width mismatch.

### Integration tests

The first integration suite will include:

1. structurally different but equivalent arithmetic modules;
2. a one-operation defect with a concrete counterexample;
3. equivalent direct and hierarchical implementations;
4. frontend-defined `Bool` or enum carriers with exact interface types;
5. record and vector packing/projection equivalence;
6. incompatible interface diagnostics;
7. rejection of `dont_care`;
8. rejection of one-hot selection without an assumption;
9. rejection of a register, memory, assertion, and DPI operation; and
10. an unknown-opcode test at the snapshot/engine boundary.

Where practical, concrete models from counterexamples will also be replayed
through generated CIRCT/Verilator fixtures. This differential check validates
the interpreter against an existing independent path without making generated
Verilog the formal semantics.

### Focused commands

Implementation will add a `tests/formal/` mirror and a focused target:

```sh
make formal-test
make check-boundaries
```

Every Racket or Rhombus invocation must use one fresh `PLTCOMPILEDROOTS`
directory for the validation batch, and direct `racket` invocations must use
`-y`. Generated bytecode, solver logs, and counterexample artifacts remain
outside version control.

## Staged roadmap

### Stage 0: package and bridge

- Add the formal package boundary and dependency documentation.
- Add the narrow `.rkt` boundary-check exception.
- Pin and document Rosette and solver setup for the focused target.
- Implement the immutable IR snapshot with explicit top-module retention.
- Test that the snapshot contains no Rhombus object handles.

Exit criterion: a verified module and hierarchy cross the language boundary
with stable names, IDs, types, drivers, locations, and origins.

### Stage 1: combinational equivalence

- Implement milestone-1a operations and strict interface matching.
- Return Rhodium-native equivalent, counterexample, unsupported, and unknown
  results.
- Add exhaustive opcode tests and integration cases.
- Add deterministic fully cared decode only after the base dispatcher is
  validated.

Exit criterion: all validation cases above pass, unsupported operations fail
closed, and at least one nontrivial ALU-sized query returns a useful result in
an interactive time budget.

## Milestone 1 implementation work breakdown

Milestone 1 is the first user-visible delivery. Stage 0 is a prerequisite, but
its package and bridge work should land in the same implementation series so
the repository never contains an uncallable engine or an unchecked language
boundary.

Milestone 1a is required for completion. Milestone 1b, deterministic fully
cared decode, is a follow-up within the same semantic milestone and must not
delay delivery of the base equivalence checker.

### Delivery order

```text
1. dependency and boundary scaffold
                 |
                 v
2. verified IR snapshot -----> 3. unsupported preflight
                 |                         |
                 v                         v
4. concrete and symbolic evaluator <------+
                 |
                 +----------> 5. hierarchy
                 |                  |
                 +------------------+
                 v
6. equivalence query and model extraction
                 |
                 v
7. public Rhombus API
                 |
                 v
8. integration, differential validation, and docs
```

Each numbered item is a review checkpoint. Do not combine all eight into one
large change: the snapshot schema, operation semantics, and public result API
need to be inspectable independently.

### Work package 1: dependency and boundary scaffold

Files:

- `rhodium/formal/README.md`
- `tools/check-boundaries.sh`
- `Makefile`
- `rhodium/README.md`
- `tests/README.md`

Tasks:

1. Add `rhodium/formal/` to the authoritative package graph as an optional
   consumer of core.
2. Extend boundary source scanning to cover the new Racket engine where
   necessary.
3. Enforce that formal code cannot import frontend or backend implementation
   modules and that core, frontend, backend, and language assembly cannot
   import formal code.
4. Permit exactly the planned Rosette interoperability module under
   `rhodium/formal/`; do not allow arbitrary new `.rkt` files throughout `rhodium/`.
5. Add `FORMAL_TESTS` and `make formal-test`. Keep it out of `host-test` and
   `test` until CI installs the pinned dependency reliably.
6. Document a clean Rosette installation command, supported Racket version,
   solver version, and a one-command environment probe.
7. Validate Rosette installation on a clean temporary Racket user directory.
   The implementation spike encountered an unavailable catalog source for a
   transitive WebSocket dependency, so CI setup must be proven rather than
   assumed. Do not vendor Rosette or that dependency without a separate
   decision.

Checkpoint:

- `make check-boundaries` passes;
- ordinary host targets still run without Rosette installed; and
- `make formal-test` fails early with an actionable dependency message when
  Rosette or the solver is absent.

### Work package 2: verified immutable IR snapshot

Files:

- `rhodium/formal/snapshot.rhm`
- `tests/formal/snapshot-test.rhm`

The snapshot is an internal language-boundary format, not a second public IR.
It will carry a schema version so incompatible engine and adapter revisions
fail explicitly.

The snapshot must contain:

- schema version;
- explicit top-module ID;
- every reachable module's ID and name;
- ordered input and output port records;
- port names, diagnostic type descriptions, layout metadata, and packed
  widths;
- operation IDs, opcodes, ordered operands, ordered results, and ordered
  places;
- each place's final driver value ID;
- only the concrete attributes needed by milestone-1 interpretation;
- referenced child-module IDs for instances; and
- module, location, and origin text for diagnostics.

Tasks:

1. Normalize a `DesignElaboration` or completed `Module` into an explicit
   `(design, top)` pair.
2. Reject raw `Design` values and incomplete or foreign top modules before
   crossing into Racket.
3. Run `verify_design` at the entry boundary even when the frontend already
   verified the design.
4. Walk only modules reachable from the selected top and preserve declaration
   order for ports and instance bindings.
5. Convert data to immutable cross-language primitives. Do not pass `Design`,
   `Module`, `Operation`, `Value`, `Place`, or hardware-type objects into
   Rosette.
6. Check exact interface type compatibility on the Rhombus side with
   `type_equal` before snapshotting. The snapshot retains descriptive type
   kind, layout, text, and packed width for interpretation and diagnostics,
   but does not attempt to serialize nominal type identity or pass a hardware-
   type object across the boundary.
7. Give the engine no mutable references back into the source design.

Snapshot tests must cover:

- one flat combinational module;
- a forward-readable wire whose defining operation precedes its final driver;
- a two-level hierarchy;
- record and vector widths and layout metadata;
- source location and origin propagation;
- explicit top selection in a multi-module design;
- rejection of a raw multi-module `Design`; and
- rejection of an unverified or incomplete module.

Checkpoint:

- the snapshot is stable under repeated traversal of the same design;
- no printed-IR parsing or Rhombus structure reflection is used; and
- a Racket-side recursive check confirms that every leaf is an allowed plain
  value.

### Work package 3: semantic coverage and unsupported preflight

Files:

- `rhodium/formal/engine.rkt`
- `tests/formal/preflight-test.rkt`

Preflight runs before symbolic inputs or a solver are created. It validates
the snapshot version, strict interfaces, packed widths, operation coverage,
required attributes, result arity, final drivers, and reachable hierarchy.

Tasks:

1. Define one explicit milestone-1a opcode table. Do not derive solver support
   from `OperationSchema.kind`; a combinational category does not prove that
   formal semantics has been implemented.
2. Associate every supported opcode with its required snapshot fields and
   result arity.
3. Reject every opcode outside the table, including unsupported operations
   with no result such as assertions, writes, and DPI calls.
4. Reject partially specified operation cases before evaluation.
5. Produce one structured diagnostic containing module, opcode, operation ID,
   location, origin, and reason.
6. Treat malformed snapshots as engine-contract errors, distinct from a valid
   `unsupported` design.

Preflight tests must independently exercise every unsupported category listed
earlier in this plan. A test for an unknown opcode will use a hand-built plain
snapshot instead of corrupting verified Rhodium IR.

Checkpoint:

- no unsupported design reaches Rosette evaluation; and
- every rejection identifies the first unsupported reachable operation
  deterministically.

### Work package 4: packed evaluator and primitive semantics

Files:

- `rhodium/formal/engine.rkt`
- `tests/formal/operation-test.rkt`

Implement one evaluator that accepts concrete or symbolic Rosette bitvectors.
Do not create separate concrete and symbolic semantic dispatchers.

Implementation order:

1. input values, constants, wires, drives, and outputs;
2. bitwise operations;
3. modular arithmetic;
4. equality and signed or unsigned comparisons;
5. total mux lookup;
6. concat, extract, extension, and truncation;
7. unequal-width shifts and overshifts;
8. equal-width casts; and
9. record and vector construction and projection.

Evaluator requirements:

- evaluate by value dependency, not operation list order, so forward wires are
  sound;
- memoize values within one module invocation;
- detect a malformed recursive dependency defensively even though verified IR
  has no combinational cycle;
- use Rosette bitvector operations throughout and avoid mixing integer and
  bitvector theories in the query;
- convert comparison booleans back to one-bit Rhodium bitvectors;
- preserve exact fixed-width overflow;
- implement signedness only where the Rhodium opcode specifies it; and
- implement record and vector layout directly from snapshot metadata.

Operation tests will exhaustively enumerate all inputs for widths one through
four where feasible. Expected results must come from small independent host
oracles, not from calling the same Rosette operation through another wrapper.
For shifts, explicitly cross value widths and amount widths on both sides of
equality.

Checkpoint:

- every supported opcode has concrete exhaustive coverage and at least one
  symbolic identity or counterexample test; and
- removing or changing an opcode case causes a focused operation test to fail.

### Work package 5: hierarchical interpretation

Files:

- `rhodium/formal/engine.rkt`
- `tests/formal/hierarchy-test.rhm`

Tasks:

1. Bind child input ports to the evaluated drivers of instance-input places in
   declared child-port order.
2. Evaluate each child definition in a fresh local value environment while
   preserving the parent's symbolic terms.
3. Map child outputs to instance result values in declared output order.
4. Cache all outputs of one instance evaluation so multiple parent users do
   not re-interpret the child independently.
5. Key caches by instance occurrence as well as child definition; two
   instances of the same module with different inputs must not alias.
6. Preserve the full instance path in diagnostics.

Hierarchy tests must include:

- direct logic versus one child instance;
- two instances of the same child with different inputs;
- two nested levels;
- a child with multiple outputs used in the parent; and
- a counterexample whose differing output passes through hierarchy.

Checkpoint:

- replacing a direct implementation with an equivalent hierarchy proves
  equivalent; and
- intentionally swapping one instance input produces a replayable
  counterexample.

### Work package 6: equivalence query and counterexample extraction

Files:

- `rhodium/formal/engine.rkt`
- `tests/formal/query-test.rkt`

Tasks:

1. Compare exact port-name sets and packed widths after Rhombus-side type
   equality has succeeded.
2. Allocate one query-local symbolic bitvector per paired top input.
3. Evaluate both top modules with the same symbolic input terms.
4. Construct one mismatch formula as the disjunction of all paired output
   inequalities.
5. Run the query with isolated Rosette verification-condition and solver state.
6. Classify `UNSAT`, `SAT`, and solver `UNKNOWN` without using exceptions as
   normal result control flow.
7. On `SAT`, evaluate all top inputs and all differing outputs in the model and
   convert packed bitvectors to nonnegative host integers.
8. Retain output type and width metadata so the Rhombus wrapper can render
   values without consulting Rosette objects.
9. Ensure no symbolic term, solver, or model object crosses the public API.

Tests must not assert one particular satisfying assignment because solver
models may vary. Each counterexample test will instead replay the returned
inputs through concrete interpretation and confirm the reported output
difference.

The internal result classifier will be testable with injected solver outcomes
so `unknown` handling does not depend on a flaky real timeout.

Checkpoint:

- commutativity or another structurally different identity returns
  `equivalent`;
- an injected one-operation defect returns a replayable counterexample; and
- two successive checks cannot leak assumptions, assertions, symbols, or
  models into one another.

### Work package 7: public Rhombus result API

Files:

- `rhodium/formal/main.rhm`
- `tests/formal/api-test.rhm`

The public wrapper owns exact Rhodium type checking and converts the plain engine
result into these conceptual objects:

```text
FormalResult(status, assignments, differences, diagnostic, assumptions)
FormalAssignment(port, type, packed_value)
FormalDifference(port, type, left_value, right_value)
FormalDiagnostic(module_path, opcode, operation_id,
                 location, origin, message)
FormalInputAssumption(port, value, care)
FormalOutputPattern(port, value, care)
FormalOutputWitness(port, type, packed_value)
FormalQuery(assumptions)
FormalReachabilityResult(status, target, assignments, output,
                         diagnostic, assumptions)
FormalPropertyResult(status, target, assignments, output,
                     diagnostic, assumptions)
```

Result invariants:

- `equivalent` has no assignments, differences, or diagnostic;
- `counterexample` has every top input assignment and at least one output
  difference;
- `vacuous` has a diagnostic identifying unsatisfiable assumptions and no
  partial model;
- `unsupported` has exactly one formal diagnostic and no partial model;
- `unknown` has a diagnostic explaining the solver outcome and no claim about
  equivalence; and
- packed values are nonnegative integers that fit their declared widths.

Reachability adds these invariants:

- `reachable` has every top input assignment and one complete output witness;
- `unreachable` has no assignments, witness, or diagnostic; and
- vacuous, unsupported, and unknown reachability results have no partial
  witness.

Output properties add these invariants:

- `proved` has no assignments, witness, or diagnostic;
- `counterexample` has every top input assignment and one complete violating
  output witness; and
- vacuous, unsupported, and unknown property results have no partial witness.

Tasks:

1. Validate accepted subject kinds and exact interface types before taking a
   snapshot.
2. Keep Rosette module names and Racket pair-list details private.
3. Provide deterministic ordering by top port declaration order.
4. Include a concise human-readable `describe()` method without making text
   parsing part of the API.
5. Export formal APIs only from `rhodium/formal/main.rhm`; do not add them to
   `#lang rhodium` or `#lang rhodium/base` in milestone 1.

Checkpoint:

- an ordinary Rhombus test can import the formal module, compare two frontend
  elaborations or completed core modules, inspect typed results, and replay a
  counterexample without importing Racket or Rosette directly.

### Work package 8: integration, differential validation, and documentation

Files:

- `tests/formal/equivalence-test.rhm`
- focused fixtures under `tests/formal/`
- `rhodium/formal/README.md`
- the owning architecture and test documents listed earlier

Tasks:

1. Add every integration case in the validation strategy.
2. Reuse existing examples when they isolate the required behavior; create
   small formal fixtures only for structure or faults not owned by an example.
3. For several SAT models, replay concrete input assignments through the
   Rosette evaluator and through a focused CIRCT/Verilator fixture.
4. For selected UNSAT identities, exhaustively enumerate a reduced-width form
   as an independent finite check.
5. Document exact supported and unsupported operation matrices, setup, API,
   result interpretation, and the distinction between behavioral and textual
   equivalence.
6. Record a non-gating benchmark for an 8-bit ALU and one hierarchical design.
   Do not introduce a brittle wall-clock assertion into tests.
7. Run the focused formal target and package boundaries using one fresh
   `PLTCOMPILEDROOTS` for the batch.

Checkpoint:

- the focused target passes from a clean checkout with the documented pinned
  dependencies;
- the ordinary host suite remains Rosette-independent;
- `git diff --check` and `make check-boundaries` pass; and
- generated bytecode, solver binaries, logs, and counterexample artifacts are
  absent from version control.

### Milestone 1a acceptance checklist

Milestone 1a is complete only when all of the following are true:

- [x] `check_equivalent` accepts `DesignElaboration` and completed `Module`
  subjects and rejects ambiguous raw designs.
- [x] Exact input and output names and Rhodium types are checked before solving.
- [x] Every milestone-1a opcode has documented Rosette semantics and focused
  exhaustive tests.
- [x] Aggregate packing and unequal-width shift semantics match the core
  contract.
- [x] Hierarchy is interpreted compositionally through explicit ports.
- [x] Equivalent, counterexample, unsupported, and unknown results are
  distinct and inspectable from Rhombus.
- [x] Counterexamples replay and demonstrate at least one actual output
  mismatch.
- [x] Unsupported operations fail before symbolic evaluation with source-aware
  diagnostics.
- [x] Repeated queries have isolated solver and symbolic state.
- [x] At least one structurally different arithmetic design, one aggregate
  design, and one hierarchical design prove equivalent.
- [x] At least one defect in each of those categories produces a
  counterexample.
- [x] `make formal-test`, focused differential checks, `git diff --check`, and
  `make check-boundaries` pass in the validated development environment.
- [x] A clean checkout can install the pinned Rosette dependencies and
  reproduce both formal targets without a linked package workaround.
- [x] Ordinary Rhodium elaboration and host tests do not load or require Rosette.

### Milestone 1b acceptance checklist

- [x] Fully cared `rtl.decode` cases and defaults execute with exact packed
  bitvector semantics.
- [x] Partially cared input cubes match exhaustively and remain independent of
  case order because verified and preflight snapshots reject overlaps.
- [x] Flat and aggregate selectors and results preserve canonical packing.
- [x] Malformed rows, values, masks, defaults, and overlaps fail as snapshot
  contract errors.
- [x] Any uncared output bit returns `unsupported` before solver invocation.
- [x] A structurally different exact-key mux proves equivalent to a partial-
  cube decode, while a defective row produces a replayable counterexample.
- [x] CIRCT and Verilator exhaustively agree with the Rosette semantics for the
  reduced-width decode fixture.

Milestone 1b does not add uncared output semantics or assumptions implicitly.

### Stage 2: assumptions and property queries

- [x] Add explicit packed top-input assumptions as a separate query object.
- [x] Detect unsatisfiable assumptions explicitly instead of reporting
  vacuous equivalence.
- [x] Preserve assumptions in public results and constrained counterexamples.
- [x] Permit `rtl.onehot_mux` only when an explicit query proves the
  exactly-one precondition for every reachable use.
- [ ] Generalize validity obligations to other partial operations only after
  their individual contracts and quantifiers are explicit.
- [x] Add packed named-output reachability with concrete witnesses and no
  change to ordinary Rhodium elaboration.
- [x] Add a universal combinational property query without reinterpreting the
  clocked `verif.assert` operation.

Exit criterion: one-hot selection can be checked under an explicit exactly-one
assumption, removing the assumption produces `unsupported`, and combinational
output targets return replayable reachable or unreachable results while
universal targets return proved or replayable violating results, without a
different implicit semantics.

### Stage 3: single-clock bounded checking

- Define a transition system with current state, next state, and top inputs.
- Require an explicit clock domain and bound.
- Treat resetless initial register state as symbolic unless the caller supplies
  an initial predicate.
- Support an explicit reset-prefix policy rather than silently assuming reset.
- Check existing `verif.assert` guard and reset suppression at each active
  edge.
- Return cycle-indexed counterexample traces.

Exit criterion: bounded assertion checks agree with focused Verilator traces
for counters, shift registers, and a small flow-control component.

### Stage 4: memories and nondeterministic operations

- Specify array state and read/write timing for each memory primitive.
- Resolve initial contents, disabled reads, out-of-range addresses, masks, and
  collision quantification before coding them.
- Specify mutual refinement or another relational equivalence contract for
  synthesis freedom.
- Add memory and nondeterminism only with adversarial soundness tests.

Exit criterion: the documented relation distinguishes genuine mismatch from
permitted implementation choice without shared-hole or independent-hole
artifacts.

### Stage 5: sketches and synthesis

- Introduce a separate explicit sketch representation with bounded holes.
- Keep holes out of ordinary frontend values and core IR.
- Require a specification, search space, and cost objective for each synthesis
  task.
- Re-verify and, where possible, equivalence-check any synthesized result
  before exposing it as hardware.

Exit criterion: a small bounded synthesis task produces ordinary verified Rhodium
IR and a replayable proof query; no solver-specific object escapes into normal
elaboration.

## Documentation changes during implementation

Implementation will update:

- `rhodium/README.md` with the formal consumer in the authoritative package graph;
- the root `README.md` with one concise optional-formal-engine link and status
  item;
- `tests/README.md` with `tests/formal/` and `make formal-test`;
- `tools/check-boundaries.sh` with the formal dependency direction and narrow
  Racket-module exception; and
- `rhodium/formal/README.md` with the supported semantic matrix and examples.

The detailed operation catalog will live only in the formal component's
README and tests. Other documents will link to it rather than duplicate it.

## Implementation stop conditions

Pause and revise this plan instead of improvising if implementation discovers:

- a supported core operation whose semantics are not specified independently
  of CIRCT lowering;
- a need for frontend metadata to interpret verified core IR;
- a need to mutate IR or preserve Rosette objects inside it;
- ambiguity about quantifier order or choice sharing for unspecified behavior;
- a multi-clock notion of “cycle” that is not supplied explicitly by the
  caller;
- counterexamples that cannot be mapped back to stable Rhodium ports and source
  origins; or
- recurring semantic drift between the Rosette interpreter and the core
  contract.

Any of these indicates a semantic-boundary problem, not merely missing solver
plumbing.
