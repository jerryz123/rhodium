<!-- Guides contributors through RV5Stage CHI implementation ownership and validation. -->

# Developing RV5Stage CHI endpoints

Read the package [README](README.md) for public configuration, transaction,
snoop, and uncached-access contracts. This guide owns implementation placement,
dependency direction, extension workflow, and focused validation.

## Architecture and dependency boundary

The package may depend on RV5Stage cache geometry and public cache/uncached
protocols, the RISC-V physical-memory model, the shared CHI library, and public
Rhodium libraries. Transaction engines must not import either cache
implementation. `uncached.rhdl` may import the I-cache and D-cache protocol
types that form its core-facing boundary.

`foundation.rhdl` is the dependency root. Snapshot-read, refill, write-unique, snoop, and
uncached engines depend on it; writeback additionally composes write-unique.
The I-cache and D-cache instantiate the shared engines, while `rv5stage.rhdl`
owns external channel composition.

Import CHI wire types, coherence rules, channels, service metadata, and required
transaction engines from their defining modules. Do not pull NoC, Home, storage,
or monitoring implementations into the caches through the convenience facade.
The [CHI import guide](../../../chi/README.md#package-boundary-and-import) owns
the supported import contract.

Requester write-data packet construction is shared through
[`chi/protocol/messages.rhdl`](../../../chi/protocol/messages.rhdl). The foundation wrapper
retains address normalization and its DataID, CCID, and DBID-field choices;
the shared builder does not depend on core geometry or physical-memory policy.

The response wrapper uses `chi_response` from the same CHI message owner;
RV5Stage retains its zero DBID/QoS, successful status, and caller-supplied
coherent response bits. The data-cache snoop engine uses
`rv5stage_chi_snoop_address` in the foundation to restore the three omitted
address bits and truncate or zero-extend to the core address width. `rv5stage-chi-requests` compares complete responses
and narrow/equal/wide snoop addresses as well as requests.

REQ construction stays in the foundation and returns an immutable value with
the existing inactive/optional fields zero. Address normalization, SnpAttr versus
DoDWT, memory attributes, CompAck, and retry decisions remain explicit here.
`rv5stage-chi-requests` compares complete packets at all DAT widths with REQ
options on/off, independently repeated calls, and varying constructor controls.
Keep the uncached, I-cache, and D-cache fixtures as engine-level coverage.

The data-snoop engine builds its DAT immutably from its existing zero-inactive
policy, overriding only payload, routing, packet IDs, dirty response, and
request trace/QoS. The D-cache bench compares the complete DAT while stalled,
including nonzero trace/QoS.

## Implementation map

| File | Ownership |
|---|---|
| [`foundation.rhdl`](foundation.rhdl) | Response profiles, physical-region/Home configuration, RN parameters and identities, and common flit constructors |
| [`line-read.rhdl`](line-read.rhdl) | Retry-aware coherent instruction snapshots, without cache ownership |
| [`refill.rhdl`](refill.rhdl) | Retry-aware packet-complete cache-line acquisition and acknowledgement |
| [`write-unique.rhdl`](write-unique.rhdl) | One partial-width retryable `WriteUniquePtl` transaction |
| [`writeback.rhdl`](writeback.rhdl) | Serialized dirty-line drain through write-unique transactions |
| [`snoop.rhdl`](snoop.rhdl) | Data-cache snoop lifetime, DVM pairing, cache lookup/update, and response traffic |
| [`uncached.rhdl`](uncached.rhdl) | Shared one-outstanding instruction/data RN-I implementation |

The core-facing uncached protocol remains in
[`../uncached-protocol.rhdl`](../uncached-protocol.rhdl) because it is the
transport-independent boundary used by routing and the MMU. Shared cache
geometry remains in [`../cache.rhdl`](../cache.rhdl) because both private-cache
packages and these transaction engines consume it.

## Change workflow

1. Put configuration, capability descriptions, and flit construction shared by
   several engines in `foundation.rhdl`.
2. Keep each retained transaction lifetime in its owning engine; do not move
   cache arrays or replacement policy into this package.
3. Preserve selected Home, transaction identifiers, retry state, packet
   accounting, and response stability from acceptance through completion.
4. Update the package README when externally visible configuration,
   transaction, ordering, traffic, assertion, or deliberate-limit behavior
   changes.
5. Update the SoC, cache, and backend consumers together when a public CHI type
   or engine path changes.

## Focused validation

Run host-side configuration and public-shape checks together:

```sh
tools/run-racket-tests.sh \
  cores/rv5stage/tests/transaction-engines-test.rhm \
  cores/rv5stage/tests/refill-test.rhm \
  cores/rv5stage/tests/uncached-test.rhm \
  cores/rv5stage/tests/rv5stage-test.rhm
```

Use the `rv5stage-uncached`, `rv5stage-icache`, and `rv5stage-dcache` CIRCT
fixtures for cycle-visible traffic, retry, refill, writeback, and snoop
behavior. Include the composed RV5Stage or SoC owner when configuration or
external endpoint integration changes. Run `make check-boundaries` after
moving modules or changing dependency direction. The backend fixture
[`DEVELOPING.md`](../../../tests/backend/DEVELOPING.md) owns runner modes and
artifact policy.
