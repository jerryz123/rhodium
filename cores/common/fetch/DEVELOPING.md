<!-- Defines maintenance ownership and behavioral validation for shared physical fetch routing. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing physical fetch routing

Read [README.md](README.md) for the public timing and replay contract and
[common ownership](../DEVELOPING.md) for package boundaries.

`protocol.rhdl` owns physical request attributes, lookup outcomes, and cancellable
read endpoints. `memory.rhdl` owns the S1/S2 pipe and one pending/completed
uncached owner. It uses the neutral L1I protocol and Flow, never a pipeline's
fetch bundle. Translation faults are adapted in the consuming MMU.

Keep full physical-request matching and update priority explicit: flush wins,
then read acceptance, response capture, and completed-reply consumption or
discard. Do not add a hit-path queue or cancel accepted transport ownership.
The shared cache attachment owns transport draining; the frontend selects which
repair, redirect, and invalidation events cancel its lookup owner.

Run the behavioral router fixtures after changing these contracts:

```sh
python3 tools/testing/rsim/run.py --fixture instruction-memory32 --fixture instruction-memory64
```

They cover cached throughput, explicit replay, full-width replies, S1 kill,
exactly-once uncached reads, stale address/attribute replacement, access faults,
and independent invalidation/cancellation. Integration changes additionally use
`rv5stage-fetch-throughput`, `rv5stage-mmu-replay`, `rv5stage-uncached`,
`rv5stage-io-mshr`, `rv2wide-fetch`, and `rv3wide-fetch`. Run the repository
boundary audit after changing imports or ownership. Tests and generated native
models follow the [test guide](../../../tools/testing/DEVELOPING.md).
