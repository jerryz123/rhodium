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

`foundation.rhdl` is the dependency root. Refill, write-unique, snoop, and
uncached engines depend on it; writeback additionally composes write-unique.
The I-cache and D-cache instantiate the shared engines, while `rv5stage.rhdl`
owns external channel composition.

Requester write-data packet construction is shared through
[`chi/messages.rhdl`](../../../chi/messages.rhdl). The foundation wrapper
retains address normalization and its DataID, CCID, and DBID-field choices;
the shared builder does not depend on core geometry or physical-memory policy.

## Implementation map

| File | Ownership |
|---|---|
| [`foundation.rhdl`](foundation.rhdl) | Response profiles, physical-region/Home configuration, RN parameters and identities, and common flit constructors |
| [`refill.rhdl`](refill.rhdl) | Retry-aware packet-complete cache-line acquisition and acknowledgement |
| [`write-unique.rhdl`](write-unique.rhdl) | One partial-width retryable `WriteUniquePtl` transaction |
| [`writeback.rhdl`](writeback.rhdl) | Serialized dirty-line drain through write-unique transactions |
| [`snoop.rhdl`](snoop.rhdl) | Clean/data snoop lifetime, DVM pairing, cache lookup/update, and response traffic |
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
