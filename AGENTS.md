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
- Run every Racket or Rhombus test, elaboration, and fixture command with
  `PLTCOMPILEDROOTS` set to a newly created temporary directory. Do not append
  a trailing path-list separator: that restores source-adjacent `compiled/`
  directories as fallback roots and can load stale bytecode. Reuse the same
  temporary root within one focused validation batch so dependencies are not
  repeatedly rebuilt.
- Add `-y` when invoking `racket` directly so changed dependencies are rebuilt.
  The only exception is `tools/run-racket.sh` after it verifies an immutable
  bytecode artifact for the exact commit, Racket and Rhombus versions, platform,
  and workspace path. Treat an `instantiate-linklet` mismatch or a reference to
  a moved module as stale bytecode first, and reproduce it with a fresh compiled
  root before diagnosing the source.
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
  processor ownership and [`tests/DEVELOPING.md`](tests/DEVELOPING.md) for test
  placement, fixtures, CI, and checked-in artifacts.
- Keep public behavior and contracts in `README.md`; keep implementation
  architecture, source ownership, extension workflows, and contributor
  validation in the companion `DEVELOPING.md`.
- Link to the owning document instead of duplicating catalogs or contracts.
