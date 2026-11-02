<!-- Defines RV3Wide issue/writeback ownership and focused behavioral validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RV3Wide

Read [README.md](README.md) for the implemented interface and its current limits,
then the [core ownership rules](../DEVELOPING.md) and repository
[RTL style](../../RTL_STYLE.md).

## Ownership

RV3Wide owns three-slot admission, operand-path allocation, architectural age,
retirement qualification, and physical-write coalescing. Shared
`cores/register-file.rhdl` remains policy-free storage. Do not add reset or
internal forwarding there, or depend on another named core's pipeline/config.

`issue.rhdl` builds the longest resource-compatible prefix using decoded
source-use controls and externally supplied readiness. Same-group dependencies
do not use same-group ALU forwarding. The dedicated/floating read geometry is
fixed, not a generic register-port allocator.

`register-access.rhdl` connects that admission boundary to 4R2W storage. Its
forwarding inputs have explicit oldest-to-youngest order. A matching unavailable
producer overrides an older ready producer; the pending scoreboard remains the
caller's owner for accepted deferred work.

`writeback.rhdl` receives already qualified successful writes. Never compute its
suppression from speculative decode, candidate validity, or a replayed/faulted
younger instruction. Keep each original result for retirement/cosim even when
its physical GPR write is suppressed.

## Validation

Run the focused behavioral fixtures through the managed Racket cache:

```sh
python3 tools/testing/rsim/run.py --fixture rv3wide-admission --fixture rv3wide-register-access
```

The admission oracle independently counts source sets, functional resources,
and distinct normal destinations. Writeback checks every three-slot successful
subset and destination combination, including x0 and a reserved completion.
The register-access test checks actual shared storage, floating-port ownership,
all nine forwarding inputs on every operand, unavailable-youngest priority,
same-cycle completion bypass, pending WAW interlocks, and qualified suppression.

Update [the package dependency inventory](../../rhodium/DEVELOPING.md) when
imports change and run `make check-boundaries`. Register SoC selections only
after the execution, precise retirement, memory, and architectural-profile
contracts are implemented and exercised.
