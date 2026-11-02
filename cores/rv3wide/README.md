<!-- Describes the implemented RV3Wide register-access boundary and its current integration limits. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV3Wide

This directory currently implements RV3Wide's three-slot issue and register-access
boundary. It is not yet an executable processor or a selectable SoC hart.
Contributor ownership and checks are in [DEVELOPING.md](DEVELOPING.md).

## Register access

Import `cores/rv3wide/register-access.rhdl` and instantiate
`RV3WideRegisterAccess()` inside a synchronous circuit. It uses the shared
`RegisterFile(XLen.X64, ~read_ports: 4)` with exactly two physical writes and
no reset of GPR contents.

Each candidate has a dedicated read. The fourth read serves the oldest
candidate needing two distinct nonzero sources. Unused dedicated reads are not
lent to other slots; forwarding does not relax this read-path budget. Immediate,
PC, unused, and x0 sources need no read. Same-group RAWs split the issued prefix.

The nine `producers` are ordered oldest to youngest:
WB slots 0–2, MEM slots 0–2, EX slots 0–2. The youngest matching producer wins;
if unavailable it blocks the operand instead of exposing an older value.
Current writes bypass storage, and a deferred completion can satisfy a source
on its write edge. A pending deferred destination still blocks a new writer
until the caller clears its scoreboard bit.

`normal_write_ports` is the caller's reservation for this group's future WB
cycle, not the current physical write availability. Admission permits at most
one memory operation, branch, and deferred operation per group. Serializing
operations issue alone after drain. Resource failures stop the entire younger
suffix.

`normal_write` and `deferred` describe possible completion paths independently.
A load that may hit in MEM or allocate a slow response must set both: its hit
still needs a normal WB port. A multiply with only a scheduled late result sets
`deferred` without `normal_write`. `write` separately qualifies its architectural
destination, including pending-WAW interlocks.

## Retirement coalescing

`retiring` supplies only successfully committing, nondeferred GPR writes.
All instruction retirement records remain the caller's responsibility.
Three writers may issue when their nonzero destinations need no more than the
reserved physical write ports. At WB, a younger successful writer suppresses
an older write to the same destination. For `x5, x6, x5`, all-success writes
the younger `x5` and `x6`; a fault/replay in slot 2 instead preserves the older
`x5` and `x6`.

An accepted deferred completion owns physical write port 1 and is not treated
as a younger instruction. The caller must reserve that port and interlock its
destination. Capacity and deferred-destination collisions are asserted.

## Integration status

These components do not yet implement instruction fetch, decode, EX/MEM/WB
storage, precise traps, memory dispatch, or service reservations. The intended
`mini-rv3wide-rv64imacb` and `simple-rv3wide-rv64imacb` selections are therefore
not registered or advertised yet. RV2Wide and RV5Stage are unchanged.
