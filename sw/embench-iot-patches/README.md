<!-- Explains the ordered deterministic-input and functional-profile adaptations for Embench-IoT. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Embench-IoT patches

The ordered [series](series) applies only to a build-local copy of
[`embench-iot/`](../embench-iot/), using the shared
[`materializer`](../../riscv/patched_submodule.py). Upstream sources retain
their individual license notices, including GPL-3.0-or-later.

The patches provide precomputed CRC32 inputs, preserve upstream local-scale
defaults behind `#ifndef LOCAL_SCALE_FACTOR`, and select the existing bounded
XGBoost sample profile with its exact correctness check. The builder passes
the requested local scale through `-DLOCAL_SCALE_FACTOR` and compiles directly
from the patched tree. No generated source overlay is needed.

Use `make -C sims embench-elfs SOC=simple CORE=rv2wide ISA=rva23` to build the
suite. See the [software contributor guide](../DEVELOPING.md) for maintenance
and validation. Preserve the workload selection, sample indices, and checks
when refreshing upstream pins.
