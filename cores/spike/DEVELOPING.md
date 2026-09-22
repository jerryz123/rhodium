<!-- Defines ownership and validation rules for the future Spike-backed core. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the Spike-backed core

Read the package [README](README.md) for the current public surface. Keep Spike
runtime, DPI, and transaction policy in this named-core package. Reuse pure
architectural descriptions from `riscv/` and implementation-neutral attachment
contracts from `cores/riscv/`; do not add Spike policy to either shared layer.

Run the focused host contract check with:

```sh
tools/run-racket-tests.sh cores/spike/tests/profile-test.rhm
```
