<!-- Guides changes to CHI transaction mechanisms. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing CHI transaction mechanisms

Read the [transactions README](README.md) for the public contract and the
[parent guide](../DEVELOPING.md) for package-wide boundaries. This guide owns
checker, retry, and requester-engine maintenance.

## Architecture and ownership

These modules contain checking and execution mechanisms; they are not all
monitors. Preserve each transaction lifetime and keep endpoint-specific
policy with its caller. Directory boundaries add no RTL hierarchy or facade.

## Implementation map

| Files | Responsibility |
|---|---|
| [`monitor.rhdl`](monitor.rhdl), [`data-checks.rhdl`](data-checks.rhdl) | Link/channel attachments and shared packet assertions |
| [`transaction.rhdl`](transaction.rhdl), [`coherent-transaction.rhdl`](coherent-transaction.rhdl) | Bounded non-coherent and coherent transaction checkers |
| [`retryable-transaction.rhdl`](retryable-transaction.rhdl) | Response profiles and Protocol Credit/retry association |
| [`read-once.rhdl`](read-once.rhdl), [`read-stream.rhdl`](read-stream.rhdl) | Snapshot line reads and ordered streaming reads |
| [`cache-maintenance.rhdl`](cache-maintenance.rhdl) | Dataless maintenance requester |

## Change workflow

### Requester engines

`read-once.rhdl` owns one complete RN-I snapshot-read lifetime: retry
association, packet receipt, DBID consistency, `CompAck`, and retained
completion. It deliberately receives physical Home, node, and transaction
identity from its caller; address maps, ID allocation, and cache-fill policy
remain outside the engine.

`read-stream.rhdl` owns CHI streaming-read policy: line address sequencing, the
combined outstanding/reorder slot lifetime, restart handling for late CHI
completions, consumer-deadline underflow, fixed TxnID-to-engine mapping, channel
arbitration, and RSP/DAT routing. It composes `CHIReadOnce` rather than exposing
an intermediate memory protocol.

### Monitor attachments and transaction checks

Monitoring attachments in `chi/transactions/monitor.rhdl` separate credited transport checks,
shared packet checks, and accepted-event transaction attachment. Both credited
and ready-valid wrappers call the same coverage validation and transaction
entry points. Keep coverage derived from the actual capabilities and delivered
checker behavior, not a separate profile field. Reject requested coverage
that would otherwise leave an advertised transaction class unchecked.
Private transaction-checker circuits isolate state and register names when
multiple attachments observe independent endpoints or the same event stream.
Ready-valid attachment checks explicit endpoint metadata using the same
`endpoint_pair_legal` predicate as link compatibility. It does not change
the channel's wire schema or infer peer metadata from arbitrary wiring.

Packet checkers take the concrete parameterized flit rather than a parallel
list of its fields. Identity selection remains explicit at each attachment;
credited calls use valid while ready-valid calls use accepted transfers.
Do not change activation, credit state, assertion labels, or transaction
coverage during packet-API cleanup. Internal non-coherent transaction tables
use typed zero literals for their Free state, matching coherent tables;
allocation and progression remain explicit record construction/updates.

`data-checks.rhdl` owns shared unelided-DAT and full-copyback mask/state assertions. Callers pass concrete
flits and retain event gating, assertion prefixes, and profile-specific DataID
restrictions. It owns no receipt state or transaction lifetime.

The coherent checker observes accepted RX RSP events as well as REQ/DAT to
associate copyback grants. Keep capability coverage limited to the delivered
no-retry copyback lifetime; specialized retry engines remain separate.

### Retry response profiles

Response effect decoding and milestone testing belong to `CHIResponseProfile`.
Keep public free-function compatibility entry points delegating to the methods.
Milestone names are nonempty; constructor checks retain declaration membership,
uniqueness, reserved opcodes, and retry-only profiles.

## Focused validation

Keep host tests and authoring fixtures in [`../tests/`](../tests/), and
behavioral benches in [`../tests/circt/`](../tests/circt/).

- Monitor changes: `chi-transaction`, `chi-transaction-sn`, and `chi-coherent`
  mirror credited events through ready-valid attachments. `chi-channel-monitor`
  covers stalls, reset, retirement, duplicate TxnIDs, wrong identity, and early
  DAT. The channel-monitor host test covers incompatible contracts, unsupported
  requested coverage, and opt-out; keep transport activation in `chi-monitor`.
- Copyback checking: `chi-coherent` plus both Home fixtures cover complete and
  Invalid returns, early data, duplicate IDs, inconsistent state, masks, and
  backing errors.
- Requester engines: `chi-read-stream` for sequencing/restart/reorder policy;
  `chi-read-once` for retry, packet receipt, and `CompAck`.
- Response profiles: the response-profile host test,
  `chi-response-profile`, `chi-retryable-transaction`,
  `chi-cache-maintenance`, and RV5Stage D-cache fixture.

For source moves, update direct consumers, docs, and build/CI paths together.
Run `make check-boundaries` after module or dependency changes and use the
repository wrappers for affected checks.
