<!-- Routes shared CSR implementation, architectural helpers, and core-integration validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing shared CSR/trap machinery

Read [README.md](README.md) for timing and caller obligations. This package
owns reusable state implementation, not a core's pipeline policy.

## Ownership and implementation

- `config.rhm` selects bank specialization using existing architectural types.
  It also owns the bank's delegation masks and exports static WARL choices;
  RTL and resolved configuration metadata consume the same values.
- `protocol.rhdl` defines the neutral command and architectural action enum.
- `file.rhdl` owns state, CSR dispatch, trap/return transitions, interrupt
  selection, FP state, counters, and passive cosim observations.
- `vector.rhdl` owns retained vector configuration, sticky saturation, and VS.

Use descriptors in `riscv/isa/` and helpers in `riscv/rtl/`; do not duplicate
CSR addresses or field encodings. Shared implementation must not import a
named core. See the [package graph](../../../rhodium/DEVELOPING.md).
Named adapters map their decode controls to `RiscvCsrAction`. The bank must
not learn about WB bundles, stalls, replay, or deferred completion.

Preserve CSR write priority over counting and synchronous-trap priority over
interrupts. Keep passive observation names/timing stable; RV5Stage exposes the
bank directly as its CSR observation component without a hierarchy wrapper.
Retirement counts come from the caller's successful prefix, not command success.
Both base and retirement-event HPM counters consume the full count; detect HPM
overflow with an addition carry, including an increment of two from -2.
The legacy passive `retired` observation remains a Boolean nonzero-count event.
The passive HPM taps expose implemented counter 3's pre-edge raw count and
overflow pulse to simulation. They are external-input evidence, not snapshots
of deterministic selector or pending state; no observer feeds back into the bank.

## Change workflow and validation

Update the public contract when ports or timing change, migrate callers, and
run the affected shared fixtures in `../tests/`. Generated RTL stays untracked.

```sh
tools/run-racket-tests.sh cores/riscv/tests/csr-test.rhm
FIXTURES='riscv-csr riscv-hypervisor-csr riscv-sstc-rv32' bash tools/testing/circt/run.sh --simulate-only
```

Use `riscv-zihpm-rv32`/`riscv-zihpm-rv64` for zero-counter aliases and
`riscv-sscofpmf-rv32`/`riscv-sscofpmf-rv64`/`riscv-sscofpmf-rv64h` for counting,
filters, and overflow. `rv5stage-vector-control`, `rv5stage-core`,
`rv5stage-interrupt`, and `rv5stage-wfi` cover adapter/precise-WB integration.
Run cosim pass tests if observation metadata changes, and
`make check-boundaries` after moving modules or changing imports.
`rv2wide-core` covers dual-retire accounting and the precise shared-bank WB cut;
`rv2wide-fetch` executes a trap handler through the production fetching top.
