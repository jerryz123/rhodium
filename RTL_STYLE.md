<!-- Defines consistent first-party RHDL authoring conventions for readable control, datapaths, and storage. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RTL style guide

Write RTL so a reader can identify the hardware structure, state owners, and
cycle-visible decisions without reconstructing them from syntax. Read
[`DEVELOPING.md`](DEVELOPING.md) for repository workflow and the nearest
component guide for architectural and timing invariants.

## Scope and authority

This is the authoritative style guide for first-party RHDL hardware, examples,
fixtures, and host elaboration code that constructs hardware. Test RTL follows
these conventions but retains the descriptive-comment exemption in
[`AGENTS.md`](AGENTS.md#source-documentation). Vendored implementations and
generated RTL are outside this guide's scope. It does not prescribe a style for
unrelated host applications or replace a separately licensed implementation's
upstream conventions.

`AGENTS.md` owns mandatory source comments and execution rules. This guide owns
general RTL conventions; component `DEVELOPING.md` files own local pipeline,
protocol, and ownership invariants. Language meaning remains defined by the
[frontend contracts](rhodium/frontend/layers/README.md), not by style advice.
Do not change a component's behavior or architecture merely to match a preferred
source shape.

## Organize by responsibility and time

- Keep interfaces, configuration, and storage ownership easy to locate. Within
  each stage or engine, keep related predicates, datapath expressions,
  connections, and state updates together.
- Arrange pipeline logic in stage order and retained engines by transaction
  lifetime. Do not require every register in a large circuit to be declared
  together far from its owning logic.
- Name signals for their stage, event, or role when that distinction matters:
  `wb_admit`, `refill_complete`, and `pending_owner` communicate more than
  `condition_1`, `temp`, or `next_value`.
- Use bundles to group fields with one meaning or lifetime. Capture the context
  needed by a deferred result with its owner rather than reconstructing it from
  live pipeline inputs later.
- Extract a helper when it names a coherent combinational operation. Extract a
  circuit when it owns a useful interface, state lifetime, or independently
  reusable hardware boundary. Do not fragment a coherent pipeline solely to
  shorten its file or add hierarchy to hide a long expression.
- Distinguish host specialization from hardware decisions. A host loop that
  constructs hardware does not execute one iteration per clock. Runtime
  iteration requires explicit state and a cycle-level contract.

## Visual structure

Make pipeline stages and independent components visible before the reader
examines individual expressions. Outside `tests/`, use native block comments
for section boundaries; the test-comment exemption in `AGENTS.md` remains in
force, while the whitespace guidance still applies to test RTL.

- Introduce each major pipeline stage, independent engine, or component with
  a descriptive block comment. Put two blank lines before that comment and one
  blank line between the comment and the section's first statement.
- Name the section and explain its responsibility, timing, or ownership in a
  few concise lines. A label such as `EX` or `Memory` alone is insufficient.
  Identify elastic/cache processing stages as such; a section header must not
  imply a fixed cycle boundary that the hardware does not have.
- Within a large section, separate coherent groups such as operand selection,
  fault classification, admission, and state updates with one blank line. Add
  a shorter block comment, followed by one blank line, when the group's purpose
  is not obvious from the surrounding section and names.
- Generally put one blank line after a completed indented block before the next
  independent statement at an enclosing indentation level. This includes bundle
  construction, selection/decode arms, loops, declarations with bodies, and
  complete conditional chains. For example, separate the final field of a
  multiline bundle assignment from a following `if` statement with a blank line.
  Keep attached branches such as `elsewhen`, `otherwise`, and `if`/`match` arms
  together; do not insert a blank line merely before a closing delimiter or a
  continuation of the same expression or flow chain.
- Reserve `//` comments for local rationale inside a section, not its primary
  heading. Keep declaration comments immediately above their declarations;
  when a declaration's own comment also introduces a component, the declaration
  adjacency requirement takes precedence over the blank line after a heading.
- Keep related declarations and connections together. Do not add a banner for
  every signal, call, or register, use decorative separator lines, or split
  groups at arbitrary line-count intervals.

For example, this schematic fragment separates the execute and memory sections
without implying that naming an expression inserts storage:

```rhombus
  /* EX: Operand selection and execution.
     Uses captured operands; younger transfer cancellation does not
     change the payload captured at the stage boundary. */

  def ex_left = ...
  def ex_right = ...


  /* MEM: Physical lookup resolution.
     Pairs this instruction's translation and cache outcome;
     accepted slow work retains its own completion context. */

  def mem_outcome = ...
```

## Expression layout

Keep simple operations compact; expose distinct decisions with meaningful
names. Neither minimum line count nor a fixed 80-column wrap is the objective.

- Prefer one-line declarations, ordinary calls, assignments, and expressions
  when they express one readable operation. Do not put each argument or
  parenthesized term on a separate line merely because it exists.
- Do not compress a deeply nested mux or several independent policy decisions
  into a single line. Name the predicates or intermediate choices that explain
  the decision, then use the appropriate selection form below.
- Break extremely long expressions at meaningful logical boundaries. Keep
  related terms together and indent continuations consistently with surrounding
  code; avoid parentheses whose only purpose is to support mechanical wrapping.
- Keep bundle/record construction, decode tables, lookup arms, repeated port
  mappings, and pipeline blocks multiline when that layout exposes structure.
- Use local names that explain meaning or sharing, not an alias for every
  operator. Introducing a name does not create a register or a timing boundary.

For example, retain a simple expression as one operation:

```rhombus
def load_hit = lookup_valid & tag_match & readable
```

When several faults compete, expose the decision rather than nesting all
choices in one expression. This schematic fragment gives guest faults priority
over ordinary page faults and access faults:

```rhombus
def fault_cause = priority_mux(~default: access_fault_cause):
  guest_page_fault: guest_fault_cause
  page_fault: page_fault_cause
```

The names must reflect the actual priority contract. A layout change is not
permission to reorder overlapping conditions.

## Selection and decode

Choose notation that communicates the selection contract:

| Intent | Preferred form | Contract to preserve |
|---|---|---|
| One binary decision | `mux(condition, a, b)` | True selects `a`; false selects `b` |
| Ordered predicate choice | `priority_mux` | First true condition wins; explicit default |
| Exact-key value selection | `mux_lookup` | Unique typed keys; explicit default |
| Exact-key assignments or state transitions | `switch` | Unordered unique cases; no fallthrough |
| Ordered event updates | `when` / `elsewhen` | First true branch wins |
| Exactly-one selection | `selector.mux(choices)` | Established `OneHot` or one-hot-enum contract |
| Optional-one selection | `selector.mux(choices, ~default: fallback)` | `MaybeOneHot` absence selects the default |

Use nested binary muxes for small genuinely hierarchical choices, not as the
default representation of an entire arbitration or fault policy. Do not replace
intentional priority with one-hot selection unless exclusivity is established.
An unchecked cast is not proof of exclusivity; external selectors may require
an environment assumption or validation. For one-hot enums, use typed-key mux
arms so the member-to-lane mapping is explicit rather than positional.

Use shared [typed decode relations](rhodium/std/README.md#typed-decode) for
instruction/control tables. Compose existing component mappings instead of
adding an instruction-kind decode followed by a second runtime control decode.
Keep unused payload fields uncared behind the enable or discriminator that
makes them unobservable. Do not replace care masks with arbitrary constants
for cosmetic consistency. Conversely, do not mark an observable result as
`dont_care` merely to simplify logic.

## State, events, and reset

- Drive a register directly with `<==`, not `.next <==`. Reads observe current
  state; a register targeted by a connection denotes its next-state place.
- Group assignments with the same guard and compatible priorities into one
  event- or state-oriented branch. Do not repeat an identical per-register
  conditional chain when one shared chain expresses the transition.
- Keep independent event chains separate. Combining them into one `elsewhen`
  chain must not suppress updates that can happen simultaneously. If different
  registers require different collision priorities, retain separate chains.
- Make same-cycle collision policy explicit: release versus allocation,
  response versus replacement request, flush versus admission, and competing
  faults. Document a nonobvious priority or assert the required exclusion.
- Do not rely on source-order last-connect behavior across independent chains;
  RHDL does not provide it. Outputs, wires, and instance inputs need exhaustive
  assignments; omitted register branches hold state.
- Prefer named enum states for distinct protocol phases. Separate booleans are
  appropriate for independent facts; do not force concurrent lifetimes into one
  serialized FSM or casually change an FSM's encoding for style.
- Reset validity and control needed to establish a safe initial contract.
  Leave wide payload storage uninitialized when it is unobservable until a
  valid owner writes it. Reset payload data only when its initial contents are
  required; explain a nonobvious initialization requirement.
- Use the declared clock/reset domain and existing reset/flush mechanisms.
  Flush is not automatically reset: preserve the component's same-cycle
  transfer gating and the lifetime of accepted work.

## Handshakes and transaction ownership

- Use `endpoint.fire()` for ready-valid transfer events, not manual
  `valid & ready` checks, including checks through local control aliases.
  Keep offer eligibility, stalls (`valid & !ready`), and storage-only events
  distinct; do not replace them with transfers or use `.fire()` to construct
  the endpoint's own valid/ready wiring.
- Distinguish an offer (`valid`), a transfer (`fire()`), service admission, and
  final completion. Use names such as `offered`, `transferred`, `accepted`, and
  `completed` only for the event they actually describe. A protocol with
  admission-fault sidebands may distinguish transfer from successful admission.
- Choose `Valid`, `Decoupled`, or `Irrevocable` from the required
  [interface contract](rhodium/std/README.md#ready-valid-protocols), not naming preference. Never
  silently discard an unbackpressured event or promise payload stability that
  the producer does not implement.
- Retain the accepted request's context until its response is consumed or the
  protocol explicitly releases it. Do not route a delayed response using a
  new request's live address, destination, or control.
- State what flush/replay can cancel. Speculative work may be killed;
  irrevocable accepted work must retain its owner and drain to completion.
- Keep request offer generation distinct from status produced by that offer.
  Do not feed admission/fault feedback into its own request validity and create
  a combinational loop. Make intentional ready-path dependencies visible and
  preserve registered capacity boundaries.
- Reuse [Flow](flow/README.md) maps, filters, forks, arbiters, queues, and pipes
  where their transfer, latency, capacity, and cancellation contracts match.
  Do not add storage, handshake conversions, or register stages just to make a
  source pipeline look uniform. Retained protocol owners can require explicit
  state beyond a feed-forward Flow pipeline.
- Preserve endpoint and handle information in producer return annotations and
  typed host collections rather than reannotating each element at use sites.
  Use `inject_interface` and `eject_interface` for ordinary signal/flow
  boundaries; keep explicit links where topology or custom ownership contracts
  need both endpoint views.

## Types, widths, and operators

- Use `Bool` for predicates, enums for finite semantic choices, `Mask` for lane
  sets, and `OneHot` / `MaybeOneHot` for selector contracts. Keep those types
  through bundles and interfaces rather than repeatedly packing and recasting.
- Construct literals through their type descriptors: `Bits(width)(value)`,
  `Bool(#true)`, and `Mask(lanes)(mask)`. Reusable descriptor aliases such as
  `def Byte = Bits(8)` also construct literals with `Byte(value)`.
- Use widths derived from the configuration and actual range, including count
  endpoints and non-power-of-two capacities. Distinguish byte, word, lane,
  index, virtual-address, and physical-address quantities.
- Make sign extension, zero extension, truncation, and representation changes
  explicit at the boundary that needs them. Prefer `.sext`, `.zext`,
  `.as_bits`, `.into`, and `.is_one_of` to legacy free-function spellings.
  Casts must express a real representation boundary, not silence a type mismatch.
- Use ordinary operators directly, not qualified forms such as `bits.(>>)`. Use
  prefix `-` for fixed-width arithmetic negation, not a typed zero minus the
  operand. Preserve intentional arithmetic widths and signedness.
- Use `wrap_increment(index, capacity)` from `rhodium/std/bits.rhdl` for a
  bounded circular successor, keeping the range invariant and update event
  with the caller. Use `%` for general unsigned remainder, not as a substitute
  for that invariant or an iterative divider's cycle-level implementation.
  Keep meaningful bit-field extraction, saturation, and backward stepping
  explicit rather than forcing them through either operation.
- `Mask` is a lane set, not an integer: it supports neither arithmetic nor
  shifting. Perform a genuine numeric shift on an appropriate numeric value
  and convert at the boundary; do not use casts to disguise mask arithmetic.
- Keep raw architectural and protocol encodings at their owning boundary.
  Reuse named fields and shared decode helpers rather than duplicating magic
  bit slices or private encoders throughout a consumer.

## Datapath structure and storage

- Make the intended network visible: parallel replicated lanes, ordered mux
  chain, balanced reduction, prefix scan, or cross-cycle recurrence. Source
  brevity is not evidence of a smaller or faster implementation.
- Use [`tree_reduce`](rhodium/std/README.md#reduction-trees) for a balanced
  reduction when regrouping preserves the intended result. Check intermediate
  widths, overflow, and associativity; do not rebalance ordered floating-point
  arithmetic or a prefix scan as a cosmetic cleanup.
- Mutable host folds are valid for intentional priority selection, staged
  barrel shifting, scans, and other ordered elaboration. Explain a nonobvious
  ordering or topology rather than banning mutation or converting every fold
  into a tree. Reuse dedicated Boolean reduction operations where applicable.
- Share datapaths when sharing is part of the design, and expose intentional
  replication. Adding or removing sharing can change timing, fanout, or resource
  contention and is an architectural change, not a formatting fix.
- Keep storage kind and timing explicit. Use the owning memory abstraction for
  SRAM-like arrays; do not replace synchronous memory with register vectors,
  combinational reads, or reset loops for stylistic uniformity. Preserve port
  count, read latency, write masks, and read/write collision behavior.
- Prefer existing pipes, queues, and shift registers when their contracts fit.
  Do not replace a handshaked delay with an unconditional delay or conflate a
  payload register with elastic capacity.

## Applying the guide

Apply these rules within the requested change; do not sweep unrelated code
merely to achieve visual uniformity. Separate behavior-preserving readability
work from changes to priority, latency, storage, sharing, or cancellation.
Preserve public event/observation contracts while reorganizing source.

For each change, a reviewer should be able to identify the state owner, when
work transfers and completes, what wins a collision, which work survives a
flush, and which hardware topology is intentional. Validate refactors at the
owning behavioral boundary using the
[test policy](tools/testing/DEVELOPING.md#authoring-principles). Documentation
edits follow the [documentation checks](DEVELOPING.md#validate-at-the-owning-boundary).
This guide does not introduce a new lint gate or change the test matrix.
