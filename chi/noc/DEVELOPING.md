<!-- Maps CHI NoC compilation, attachment ownership, and integration checks. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing CHI network integration

Read the [network README](README.md) for the public contract and the
[parent guide](../DEVELOPING.md) for package-wide boundaries and validation.
This guide owns CHI-specific NoC compilation and attachment changes.

## Architecture and ownership

Keep [`noc-authoring.rhm`](noc-authoring.rhm) independent of Rhodium and CIRCT.
Generic topology, routing analysis, physical-link binding, and router hardware
remain in the root [`noc/`](../../noc/DEVELOPING.md) package. CHI owns channel
plane attachment and ejection queue policy, not a second router mechanism.
Directory boundaries add no RTL hierarchy or per-directory facade.

## Implementation map

| File | Responsibility |
|---|---|
| [`noc-authoring.rhm`](noc-authoring.rhm) | Pure CHI connection expansion and per-channel route compilation |
| [`noc-adapter.rhdl`](noc-adapter.rhdl) | Typed REQ/RSP/DAT/SNP injection, queued ejection, and endpoint helpers |
| [`noc-router.rhdl`](noc-router.rhdl) | Typed local attachment-plan compilation and three- or four-plane router-family composition |

## Change workflow

Compile typed local endpoint plans before channel closure indices. Preserve
category and column order because those indices select physical router slots.
Family adapter lookup returns concrete route/ejection decision bundles.

Share injection wiring and flow-stage bookkeeping in `noc-adapter.rhdl`, and
reuse generic envelope removal from
[`noc/rtl/route-adapter.rhdl`](../../noc/rtl/route-adapter.rhdl). Keep typed
channel circuits and fixed versus family-site factories explicit: REQ/RSP/DAT
select `tgt_id`, while SNP selects `CHISnoopDispatch.target_id` and carries only
its flit. Ejection checks stay channel-owned because SNP lacks a target field.
These helpers add no route policy or buffering beyond generic injector/ejector
behavior and the CHI one-entry ejection queue.

`CHINoCPlane` owns endpoint attachment policy. Fixed helpers and `CHIRouter`
family attachments use its injection and queued-ejection methods; keep RN/HN/SN
field mappings and fixed/family choices explicit at the callers. The queue
precedes the ejection adapter, and router availability tracks its input
readiness, not final sink readiness. `CHINoCPorts` groups existing plane
endpoints without introducing circuit parameters, ports, or hierarchy.

## Focused validation

Host tests and authoring fixtures live in [`../tests/`](../tests/); behavioral
benches in [`../tests/circt/`](../tests/circt/). Run the mixed-role
`router-attachment-plan` host test for all six roles, nonlexical columns, and
empty attachments. Run `chi-noc-adapter` for all sixteen adapter variants,
complete payloads, stalls, and invalid routes/targets/sites; its host test
covers transform metadata used by diagram/event tooling. Include the SNP,
subordinate, family-NoC, and router-composition fixtures. The SN fixture
checks fixed attachments under stalls; the family fixture checks an asymmetric
three-router path. Validate Mini/Single fixed RN-F/HN attachments and Tiled
coherent family attachments when integration changes. Run `make check-boundaries`
after module or dependency changes.
