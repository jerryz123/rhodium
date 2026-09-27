<!-- Defines mandatory execution and source-editing rules for agents working on Rhodium. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rhodium agent instructions

Read the repository [`DEVELOPING.md`](DEVELOPING.md) and the nearest component
`DEVELOPING.md` before changing architecture, ownership, tests, or generated
artifacts. This file contains the mandatory rules that apply to every change.

## File headers

- Begin every new or modified source, test, script, configuration, and
  documentation file with a concise, file-specific purpose comment using the
  format's native syntax. For Markdown, use an HTML comment.
- Keep a shebang or other mandatory first line first, with the purpose comment
  immediately after it.

## Verification

- After changes, run the minimum focused set of tests that directly covers the
  modified behavior. Use a broader suite only when the change spans its scope.
- Run every Racket or Rhombus test, elaboration, and fixture command through
  `tools/run-racket-tests.sh` or `tools/run-racket.sh`. They use one persistent,
  worktree-owned compiled root under `.rhodium-cache/`, rebuild changed
  dependencies incrementally, and clear checkout bytecode when the source-path
  inventory changes. Do not set `PLTCOMPILEDROOTS` during normal repository
  validation because doing so deliberately bypasses the managed cache. An
  external harness that must own a root may supply exactly one isolated
  directory; a trailing path-list separator restores unsafe source-adjacent
  `compiled/` fallbacks.
- Add `-y` when invoking `racket` outside the repository wrapper. Treat an
  `instantiate-linklet` mismatch or reference to a moved module as stale
  bytecode first; run `make clean-racket-cache` and reproduce it before
  diagnosing the source. Exact CI bytecode remains valid only after
  `tools/racket-artifact.sh` verifies its commit, Racket and Rhombus versions,
  platform, and workspace path.
- Test supported behavior and invalid uses of supported features. Do not add
  tests whose purpose is to prove that a removed or unimplemented feature does
  not exist.
- Keep generated output out of version control unless an owning development
  guide explicitly defines it as a checked-in reference.

## RTL formatting

- Prefer one-line RTL declarations, calls, assignments, and expressions.
- Drive a register directly with `<==`; do not spell author-level next-state
  assignments as `.next <==`. A register reads as current state and acts as
  its next-state place when it is the target of a connection.
- Use prefix `-` for fixed-width arithmetic negation; do not spell negation as
  a typed zero minus the value.
- Group assignments that share the same guard and priority into one
  state- or event-oriented `when` branch. Avoid parallel per-register chains
  that repeat an identical condition when one prioritized chain preserves the
  behavior.
- Keep conditional chains separate when their alternative events can occur
  independently or use different priorities; do not serialize simultaneous
  state updates merely to remove repeated syntax.
- Do not break an RTL expression merely because it contains a call or several
  operands. Use line breaks only for syntactic blocks, extremely long
  expressions, or regular repeated forms whose aligned layout improves the
  visible hardware structure.
- Keep record and bundle construction, lookup and decode tables, and repeated
  port or field mappings multiline when their block structure is meaningful.

## Architecture and documentation routing

- Treat [`rhodium/DEVELOPING.md`](rhodium/DEVELOPING.md) as the authoritative
  package-dependency contract. Update its dependency inventory when direct
  Rhodium imports change, and run `make check-boundaries` after moving or adding
  modules or changing dependency direction.
- Follow [`cores/DEVELOPING.md`](cores/DEVELOPING.md) for reusable-versus-named
  processor ownership and [`tools/testing/DEVELOPING.md`](tools/testing/DEVELOPING.md) for test
  placement, fixtures, CI, and checked-in artifacts.

## README and DEVELOPING structure

Use this structure for first-party documentation throughout the repository.
It describes the order and owner of information, not a requirement to repeat
these exact headings or add empty sections. The root pair introduces the
project and repository workflow; a component pair describes only that
component. A small leaf directory may link to its parent's guide instead of
creating a boilerplate companion file. Do not apply this policy to vendored or
submodule documentation.

For each `README.md`, use this reader-facing progression:

1. **Purpose and entry point:** State what the component does, when to use it,
   and its current scope in a short opening. Link to the companion or owning
   parent `DEVELOPING.md` for contributors.
2. **Get started:** Give the smallest working import, invocation, example, or
   selection path. Include only prerequisites and commands needed to use the
   component; link to the repository quick start for shared setup.
3. **Public contract:** Describe the supported API, behavior, configuration,
   observable ordering and errors, and interoperability boundaries. Organize
   component-specific detail under descriptive headings rather than a fixed
   template. A public architecture diagram is useful here only if it explains
   how a user composes or observes the component.
4. **Limits and navigation:** State deliberate unsupported cases and relevant
   compatibility constraints. Link to child packages, examples, specifications,
   and the contributor guide without duplicating their catalogs.

For each `DEVELOPING.md`, use this contributor-facing progression:

1. **Scope and reading path:** Link to the owning README for the public
   contract, whether local or in a parent, and state what maintenance this
   guide owns.
2. **Architecture and ownership:** Explain internal boundaries, dependency
   direction, invariants, and which layer owns each decision. Link to the
   repository package graph rather than restating it.
3. **Implementation map:** Map important files or subpackages to
   responsibilities. Include only enough inventory to route a change; link to
   child guides for their details.
4. **Change workflow:** Explain extension points, safe edit sequence,
   generated-artifact ownership, and how public contracts are kept current.
5. **Validation:** Give the smallest focused checks for changes to this
   component, then any broader integration or CI checks and their triggers.

Route facts by audience, not by where they were first written. Public imports,
semantics, configuration, usage commands, and observable limitations belong in
README even when their implementation is described in DEVELOPING. Source maps,
internal state machines, test-authoring instructions, CI policy, and artifact
regeneration belong in DEVELOPING. A user-facing smoke command may appear in
README; contributor test matrices and maintenance procedures belong in
DEVELOPING. Link across the pair instead of copying the same table or contract.
Keep active multi-step future work in a dedicated plan only when it needs one;
retire completed plans and move any lasting contract or maintenance rule to its
owning guide.

When editing an existing guide, place new material in the owning section and
remove nearby duplication or stale links. Do not churn an unrelated guide
solely to make its headings match this outline. Check purpose/SPDX headers,
relative paths and anchors, code fences, Mermaid blocks, documented commands,
and `git diff --check` after documentation changes.
