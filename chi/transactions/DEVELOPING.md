<!-- Guides changes to CHI transaction mechanisms. -->

# Developing CHI transaction mechanisms

Read [README.md](README.md) and the parent
[CHI developer guide](../DEVELOPING.md) before changing this area.
The parent owns package-wide boundaries; this guide owns component extension and validation.

These modules contain both checking and execution mechanisms; they are not all monitors. Preserve each transaction lifetime and keep endpoint-specific policy with its caller.

Keep tests and authoring fixtures in [`../tests/`](../tests/), and behavioral
benches in [`tests/backend/`](../../tests/backend/DEVELOPING.md).
For source moves, update direct consumers, package documentation, and build/CI
paths together. Run `make check-boundaries` and the affected host and behavioral
checks with a fresh isolated compiled root. Directory boundaries do not add RTL
hierarchy or per-directory facade modules.

## Extension and focused validation

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

The `chi-transaction`, `chi-transaction-sn`, and `chi-coherent` fixtures
mirror credited events through ready-valid attachments. The
`chi-channel-monitor` fixture checks stalls, reset, and retirement from both
requester and subordinate viewpoints; its negative cases check accepted
duplicate TxnIDs, wrong identity, and early DAT. Host
`chi/tests/channel-monitor-test.rhm` checks incompatible contracts, unsupported
requested coverage, and explicit opt-out. Keep transport activation tests
in `chi-monitor`.

`data-checks.rhdl` owns shared unelided-DAT assertions. Callers pass concrete
flits and retain event gating, assertion prefixes, and profile-specific DataID
restrictions. It owns no receipt state or transaction lifetime.

Response effect decoding and milestone testing belong to `CHIResponseProfile`.
Keep public free-function compatibility entry points delegating to the methods.
Milestone names are nonempty; constructor checks retain declaration membership,
uniqueness, reserved opcodes, and retry-only profiles. Run the response-profile
host test, `chi-response-profile`, `chi-retryable-transaction`, `chi-cache-maintenance`, and RV5Stage
D-cache fixture when changing this API or its consumers.
