#!/usr/bin/env bash
# Resolves simulator stage locations and toolchains; Moon owns prerequisite scheduling.
# SPDX-License-Identifier: Apache-2.0
set -eo pipefail
selection=(--soc "${SOC:-simple}" --isa "${ISA:-}")
if [[ -n ${CORE:-} ]]; then selection+=(--core "$CORE"); fi
axes=$(python3 socs/configs/selection.py "${selection[@]}")
read -r shape core isa <<< "$axes"
soc="$shape-$core-$isa"
backend="${RTL_BACKEND:-rsim}"
variant=normal
if [[ ${TRACE:-0} == 1 ]]; then variant=trace; fi
if [[ ${COSIM:-0} == 1 ]]; then variant="${variant/normal/}"; variant="${variant:+$variant-}cosim"; fi
if [[ ${STATS:-0} == 1 ]]; then variant+=-stats; fi
model_key="$soc"
if [[ $backend != rsim ]]; then model_key+="-$backend"; fi
if [[ $variant != normal ]]; then model_key+="-${variant#normal-}"; fi
model_dir="$PWD/.rhodium-cache/moon/models/$model_key"
work_dir="${BUILD_ROOT:-/tmp/rhodium-sims}/$model_key"
case "$work_dir" in /*) ;; *) work_dir="$PWD/$work_dir";; esac
descriptor="${PREBUILT_PROGRAM_TARGET:-${PROGRAM_TARGET:-$PWD/.rhodium-cache/program-targets/$soc.json}}"
if [[ -n ${PREBUILT_SIMULATOR:-} && -z ${PREBUILT_PROGRAM_TARGET:-} && -z ${PROGRAM_TARGET:-} ]]; then descriptor="$(dirname "$PREBUILT_SIMULATOR")/program-target.json"; fi
circt_opt="${CIRCT_OPT:-$PWD/.tools/firtool-1.155.0/bin/circt-opt}"
sail_identity=$(python3 riscv/patched_submodule.py identity --repository "$PWD" --submodule riscv/sail-riscv --series "${SAIL_PATCH_SERIES:-riscv/sail-riscv-patches/series}")
sail_prefix="${COSIM_SAIL_PREFIX:-$PWD/.tools/sail-riscv-0.16-sail-0.20.3-$sail_identity}"
cosim_build="${COSIM_BUILD_DIR:-${PREBUILT_COSIM_DIR:-$PWD/.rhodium-cache/sail-cosim/sail-0.20.3-$sail_identity}}"
cosim_config="$model_dir/cosim"

# Preserve generated compiler timestamps when a Moon cache restore returns identical input.
copy_changed() {
  mkdir -p "$(dirname "$2")"
  if ! cmp -s "$1" "$2"; then
    local temporary
    temporary=$(mktemp "$2.XXXXXX")
    cp "$1" "$temporary"
    mv "$temporary" "$2"
  fi
}
