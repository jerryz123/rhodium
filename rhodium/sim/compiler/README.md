<!-- Defines standalone compiler usage, exchange formats, and optimization defaults. -->

# Standalone simulation compiler

The C++17 compiler reads a retained simulation JSON snapshot, optimizes its graph
and writes a binary model for the [native runtime](../README.md). It needs
nlohmann/json and Boost headers; optimization itself does not need Racket.

```sh
bash rhodium/sim/compiler/run.sh --input extracted.json \
  --output model.rsim --report optimized.json --timing timing.json
```

`run.sh` caches an executable by compiler, headers, flags and source contents.
`--print-path` returns that executable. `--dump-passes DIR` retains intermediate
JSON; `--help` lists all accepted options. Keep outputs outside the checkout.

## Enabled and optional passes

| Transformation | Default | Selection |
|---|---|---|
| Typed projection lowering and semantic CSE | Enabled | `--no-semantic-cse` disables import CSE |
| Shared decoders/matcher prefixes/TLB queries, constant folding, CSE, liveness and word recovery | Enabled | `--no-optimize` disables the ordinary sequence |
| Boolean lane regrouping | Enabled | `--no-regroup` |
| Packed matcher lowering | Enabled when eligible | `--no-packed-matchers` |
| Assertion/diagnostic-root removal | Disabled | `--release` |
| Register update fusion | Enabled with optimization and release | `--no-update-fusion` |
| Constant decoder specialization and Boolean relation proofs | Disabled | `--specialize-decoders`, `--canonicalize-bit-relations` |
| Unused update-input and idle-state removal | Disabled | `--prune-stateless-inputs`, `--fold-idle-fifos`, `--fold-stationary-matchers` |
| FIFO width inference and enqueue-time derived fields | Disabled | `--narrow-fifo-payloads`, `--cache-fifo-derived` |
| Selector/matcher restructuring | Disabled | `--regroup-selector-columns`, `--split-matcher-columns`, `--simplify-matcher-masks`, `--reuse-matcher-grants` |
| Word contract programs | Disabled | `--lift-contracts`; `--eager-contracts` retains eager arms |
| Contract control grouping, handshake sharing and local optimization | Disabled | `--bundle-contract-controls`, `--share-handshake-rows`, `--optimize-contract-programs` |
| Exact control caches and proven constant-result guards | Disabled | `--cache-contract-controls`, `--guard-contract-programs` |
| Indexed datapath lifting | Disabled | `--lift-indexed-datapaths` |
| Flow grouping and ordering | Disabled | `--flow-regions`, `--flow-order` |
| Bounded pure-cone replication | Disabled | `--replicate-bytes`, `--replicate-work`, `--cone-work`; `--snapshot-prefix-work` selects bounded prefixes |
| Payload lifetime sharing and routed ownership exchange | Disabled | `--share-payload-lifetimes`, `--exchange-payload-handles` |
| General payload pool grouping | Disabled | `--payload-pool-size 2..31` |

Optional passes run only when explicitly selected and may reject ineligible
candidates. `--no-optimize` does not cancel separately requested passes.
Release relies on established index and selector preconditions; retain debug
images when checking invalid behavior. Timing reports record the actual pass
sequence and before/after operation counts.

## Semantic exchange

`rhodium-simulation-ir-v2` preserves structural types, construction/projection
operations, source conditional groups and explicit semantic contract bindings.
Types are interned bits, records and vectors. Record fields pack first-field-high;
vector element zero occupies the low bits. Each value records its packed width
and structural type. Individual demanded leaves must be acyclic even when the
aggregate graph contains feedback through independent fields.

The compiler lowers demanded projections to word operations and the private v1
execution representation. Object, register, memory, port, assertion and host
records retain their semantics. Source origins and contract metadata support
analysis but do not create runtime dispatch. Exact JSON shapes and validation
are owned by `model.cpp`, `semantic.cpp` and `../inspect.rhm`; this is a versioned
private compiler exchange, not a general interchange standard.

Binary images use explicit versions checked by the runtime. Ordinary images use
version 2; contract programs require version 3, and payload-view operations
require version 4. Do not infer compatibility from file names. Graph validation
checks widths, definitions, topology, object bindings and program limits before
writing an image.

## Contract and payload lifting

Contract programs contain bounded, total, pure word operations. Stateful queries,
SRAM accesses, effects, partial operations and escaping values remain inputs or
boundaries. Conditional mode places work in its required branch scope; shared
work stays in the common enclosing scope. Generated C has private intermediates
and no program interpreter. The reference path executes the same program eagerly.

Flow metadata alone never permits dropping captured inputs, invalid previews,
arbitration state or backpressure. Exact-key caches include every captured bit.
Shared handshake rows preserve arbitrary grant masks and first-match decoders
retain row priority and defaults.

Lifetime sharing stores an opaque payload once while retaining pipeline/queue
control. Routed exchange additionally proves legal transfers, unique ownership
and absence of intermediate body access. Inspected header fields remain inline.
`--pack-payload-handles` and `--fuse-transport-state` change eligible handle/control
layout; `--isolate-payload-readers` permits smaller proven regions, and
`--bitmap-payload-slots` selects queue-carried handles with a free bitmap.
The `--payload-lifetime-*` options select alternative proven lifetime layouts.
These remain opt-in: fewer copies can be offset by handle selection and boundary
materialization. General pool grouping is a separate transformation that also
preserves observable invalid-slot payloads.

See [implementation and validation](DEVELOPING.md) before changing a pass or its
serialized representation.
