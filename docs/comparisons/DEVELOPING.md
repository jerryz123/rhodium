<!-- Defines the evidence, rubric, update workflow, and validation policy for Rhodium comparisons. -->

# Developing the comparison suite

Read the [comparison guide](README.md) for the reader paths, system map, shared
Rhodium baseline, and cross-system conclusions. This guide owns how those
claims are researched, reviewed, and kept current.

## Method and evidence discipline

Each essay follows the same process:

1. Establish Rhodium's supported behavior from the live public core, frontend,
   verifier, and lowering contracts.
2. Establish the other system's model from official manuals, specifications,
   papers, and primary repositories.
3. Compare systems at the same semantic level. An accelerator IR is not treated
   as a complete RTL language, and a host library is not credited with behavior
   supplied only by a downstream tool.
4. Separate circuit expressivity, abstraction expressivity, static guarantees,
   and syntactic economy.

Conclusions concern supported public models, not hypothetical extensions.
Ecosystem size, vendor integrations, and exhaustive library inventories are
outside the suite's scope. An internal compiler distinction counts only when
it changes what an author can state, compose, or have checked.

Prefer stable primary documentation and source over summaries. Date the
capability snapshot in the public guide, link every external claim near its
use, and describe an inference as an inference. Do not silently carry a stale
version-specific claim into a refreshed comparison.

## Common evaluation rubric

Every comparison addresses these questions:

- What does a source program denote, and which decisions happen before the
  resulting hardware runs?
- Are widths, domains, directions, and interface identities inferred,
  structural, nominal, or statically typed?
- Does an internal representation distinction produce an author-visible,
  compositional guarantee, or only a compiler normal form?
- How are state updates, assignment conflicts, priority, and concurrency
  represented?
- Are exact literals, partial patterns, and decode tables syntax, or typed
  values that can be validated, transformed, and optimized before construction?
- Do abstractions compose as expressions, signal bundles, modules, methods,
  rules, streams, or typed transformations?
- How local is a line's meaning? Which scopes, schedulers, or inference passes
  can change it?
- Where does Rhodium gain directness, where is it merely more verbose, and
  which propositions can the other system state that Rhodium cannot?
- Which imported ideas extend exact construction, and which would change its
  semantic model?

For this suite, “clean” and “elegant” mean orthogonal rules, closure under
composition, local reasoning, proportionate notation, and guarantees that rule
out real classes of bad hardware.

## Add or refresh a comparison

1. Choose a system whose semantic model creates a material contrast; do not add
   a feature-count page for a near duplicate.
2. Record the system kind and primary question in the public comparison map.
3. Ground Rhodium statements in the current owning README. Use DEVELOPING only
   for an implementation fact that materially explains a public consequence.
4. Ground external statements in the relevant version of official manuals,
   specifications, papers, and source. Preserve the snapshot date.
5. Apply every rubric dimension that materially differentiates the systems;
   state when a dimension is not applicable.
6. Update cross-system conclusions only when the new evidence changes them.
7. Check local links, external source URLs, headings, and the shared baseline.

Keep each essay self-contained enough to read directly, but link shared
Rhodium contracts rather than reproducing them. Avoid permanent rankings and
unqualified claims about rapidly changing ecosystems.

## Implementation map and validation

| Path | Responsibility |
|---|---|
| [`README.md`](README.md) | Reader paths, comparison inventory, shared baseline, and cross-system conclusions |
| Individual `*.md` essays | One source-grounded comparison and its references |
| [`../../rhodium/README.md`](../../rhodium/README.md) | Public Rhodium package model |
| [`../../rhodium/core/README.md`](../../rhodium/core/README.md) | Public core semantics |
| [`../../rhodium/frontend/README.md`](../../rhodium/frontend/README.md) | Public elaboration and profile behavior |
| [`../../rhodium/std/README.md`](../../rhodium/std/README.md) | Public reusable-library contracts |
| [`../../flow/README.md`](../../flow/README.md) | Streaming components and typed topology composition |

For a documentation change, validate purpose comments, Markdown links and
anchors, balanced fences, table structure, and `git diff --check`. Network-check
external citations when evidence changed. A prose-only comparison update does
not require a hardware test unless it also changes the implementation whose
behavior is being described.
