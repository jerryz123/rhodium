<!-- Routes maintenance of RV3Wide's miss slots and ordered request window. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the banked L1D

Read the [banked cache contract](README.md) and [shared cache guide](../DEVELOPING.md)
for the bank geometry, shared CHI engines, and fixture commands.

## Miss ownership and ordered replay

In [`mshrs.rhdl`](mshrs.rhdl), each slot owns one retryable refill and one
writeback. S3 and S4 each offer a single allocation containing the acquisition
and its victim; an invalid victim state means no copyback. The bank arbitrates
whole allocations and rotates among free entries. The two-slot configuration uses CHI IDs
0 and 3 for refills, 1 and 4 for copybacks, and 2 for maintenance. DAT and
tagged responses identify their transaction; untagged PCrdGrant matches one
waiting home/type pair. A grant freezes only its own victim's snoop state.
Round-robin arbitration among granted victims assigns the one SRAM reader to a
whole line. The reader request names the address, way, and packet index;
after the last DAT handoff, a release request names the victim to invalidate
without reading another row. Never switch owners while a read or
backpressured data packet is outstanding.
The slot declares the trace scope once; both engines inherit it. Under the
enclosing hart, each slot has `dcache/mshr[i]/refill` and
`dcache/mshr[i]/writeback` tracks with the shared event identities
`dcache/refill` and `dcache/writeback`. The [trace hierarchy regression](../tests/mshr-trace-test.rhm)
checks both sites under each slot, not just their display names.

The bank owns one shared line of staging for unordered dirty-victim
`ReadClean` allocations. It reserves the buffer on allocation and permits the
slot to start acquiring before copyback grant. The slot offers refill packets
and a completion; the bank chooses the packet destination, holds completion
until publication is safe, and suppresses installation on discard. Route
that slot's DAT by transaction ID even while copyback owns
the SRAM; send CompAck independently of victim release. Track arrived packets
by DataID and drain them through the existing installation port only after
the owning victim completes. Keep the MSHR until every buffered row installs
or an accepted snoop to the unpublished line discards it. Never block a victim
probe on the staged line's CompAck. Only clean acquisitions with their demand
still in the ordered window can discard a line this way.

[`demand-window.rhdl`](demand-window.rhdl) holds descriptors in a circular
FIFO. Its issue port indexes relative to the retirement head, while speculative
set checks examine the live physical slots without rotating the array. Write
operands occupy a second ring: allocate on accepted Store/SC/AMO, release
only when that descriptor leaves the head. Classification alone releases
neither resource. Both use registered capacity; a full ring cannot borrow
space from a simultaneous retirement.

For multi-slot service, S2 issues the next unclassified entry in FIFO order to
the same S3 tag lookup used by the head. The classified prefix stays in the
request window until retirement. An ordinary request attaches to a covering
line acquisition, observes a sufficient hit, or allocates an MSHR. A conflicting
set, incompatible upgrade, or nonordinary request cannot be skipped. Each dirty
miss can start its writeback before the other is granted. Replay the ordered head against current permissions before consuming data or writing bytes: snoops
can invalidate previously acquired lines. The one exception is the installing
row of the active ordered load miss: enqueue the leader's response first, then
only forward to a classified ordinary load at the FIFO head matching the line
and row. A matching probe disables further forwarding for that miss. Never
hold installation or CompAck for a forwarded response or publish a partial tag.
After the ordered miss releases its context, replay ordinary loads and stores
at the head of the classified prefix only in unreserved sets. Recheck the hit
in S3; a store needs unique permission. In S4, installation has priority over
the replacement update, and refill writes and gather/copyback reads have
priority over the store's masked write. The write and ACK are one transfer.
If no response slot remains for the hit, leave it replayable in S3 rather
than blocking probes in S4. Atomics and ownership upgrades cannot retire this way.
Held packets retain their own install set/way.
The `cache-banked-l1d` fixture fills B before A, reuses B’s CHI ID for C, and checks
ordered data and contexts under backpressure. For stores it also snoops B
before mutation, invalidates it, and checks masked writes after reacquisition.
The two-dirty-victim case retries B, grants it first, and checks
B-first installation with ordered responses and bytes. It probes A before A’s
grant in one run and grants A while B's first data packet is stalled in another.
The buffered-load case withholds both copyback grants through reordered
replacement DAT and CompAck. It probes either the dirty victim or the staged
clean replacement, checks every returned row, and verifies older-first responses.
