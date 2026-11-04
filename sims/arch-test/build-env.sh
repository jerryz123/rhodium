#!/usr/bin/env bash
# Resolves ACT source, generator, and platform paths for independent Moon build stages.
# SPDX-License-Identifier: Apache-2.0
set -eo pipefail
selection=(--soc "${ACT_CONFIGURATION:-${SOC:-simple}}" --isa "${ISA:-}")
if [[ -n ${CORE:-} ]]; then selection+=(--core "$CORE"); fi
axes=$(python3 socs/configs/selection.py "${selection[@]}")
read -r shape core isa <<< "$axes"
act_configuration="$shape-$core-$isa"
act_root="${ACT_BUILD_ROOT:-/tmp/rhodium-arch-test}"
case "$act_root" in /*) ;; *) act_root="$PWD/$act_root";; esac
act_source="$PWD/.rhodium-cache/moon/act/source"
act_tests="$PWD/.rhodium-cache/moon/act/tests"
act_config="$act_root/configs/$act_configuration"
act_work="$act_root/work/$act_configuration"
act_python="${ACT_PYTHON:-${ACT_VENV:-$PWD/.tools/act-venv}/bin/python}"
sail_identity=$(python3 riscv/patched_submodule.py identity --repository "$PWD" --submodule riscv/sail-riscv --series "${SAIL_PATCH_SERIES:-riscv/sail-riscv-patches/series}")
act_sail="${ACT_SAIL:-$PWD/.tools/sail-riscv-0.16-sail-0.20.3-$sail_identity/bin/sail_riscv_sim}"
