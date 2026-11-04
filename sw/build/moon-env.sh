#!/usr/bin/env bash
# Resolves software artifact locations and validates shared-archive contracts for Moon builders.
# SPDX-License-Identifier: Apache-2.0
# Source from the repository root; recipes and suite dependencies live in Moon.
set -eo pipefail
repo_dir="$PWD"
selection=(--soc "${RHODIUM_BUILD_SOC:-${SOC:-simple}}")
if [[ -z ${RHODIUM_BUILD_SOC:-} ]]; then
  selection+=(--isa "${ISA:-}")
  if [[ -n ${CORE:-} ]]; then selection+=(--core "$CORE"); fi
fi
axes=$(python3 socs/configs/selection.py "${selection[@]}")
read -r shape core isa <<< "$axes"
build_soc="$shape-$core-$isa"
software_target="${PREBUILT_PROGRAM_TARGET:-${PROGRAM_TARGET:-$repo_dir/.rhodium-cache/program-targets/$build_soc.json}}"
if [[ -n ${PREBUILT_SIMULATOR:-} && -z ${PREBUILT_PROGRAM_TARGET:-}${PROGRAM_TARGET:-} ]]; then
  software_target="$(dirname "$PREBUILT_SIMULATOR")/program-target.json"
fi
build_root="${PROGRAM_BUILD_ROOT:-/tmp/rhodium-program-tests}"
config_root="${PROGRAM_CONFIG_ROOT:-$build_root/$build_soc}"
output="$config_root/${RHODIUM_BUILD_SUBDIR:?Moon must supply the software output subdirectory}"
archive_args=()
if [[ -n ${RHODIUM_ARCHIVE_KEY:-} ]]; then
  targets="${RHODIUM_ARCHIVE_TARGETS:-${PROGRAM_TARGETS:?Archive builds require target descriptors}}"
  archive_args=("$RHODIUM_ARCHIVE_KEY" --targets "$targets")
  # Validate the exact target/group before invoking a compiler, not just at packing.
  python3 -m tools.ci.archives prepare "${archive_args[@]}" --soc "$build_soc"
  software_target="$targets/$build_soc.json"
  config_root="$repo_dir/.rhodium-cache/moon/build/$RHODIUM_ARCHIVE_KEY"
  output="$config_root/$RHODIUM_BUILD_SUBDIR"
  if [[ ${RHODIUM_ARCHIVE_KIND:-} == platform ]]; then
    output="$config_root"
    if [[ -n ${PROGRAM_TARGETS:-} ]]; then
      output="/tmp/rhodium-platform-builds/$RHODIUM_ARCHIVE_KEY"
      OPENSBI_LLVM="${OPENSBI_LLVM:-/usr/bin}"
    fi
  fi
fi
compiler="${RISCV_CC:-riscv64-unknown-elf-gcc}"
python="${PYTHON:-python3}"

# The builder owns compilation and its manifest; this only packages a completed artifact.
finish_software_build() {
  if [[ -n ${RHODIUM_ARCHIVE_KEY:-} ]]; then
    python3 -m tools.ci.archives pack "${archive_args[@]}" --manifest "$output/manifest.json"
  fi
}

# Match the supported public configuration boundary before doing expensive build work.
require_software_shape() {
  for allowed in "$@"; do
    if [[ $shape == "$allowed" ]]; then return; fi
  done
  echo "This software recipe requires SoC shape: $* (got $shape)" >&2
  return 2
}
