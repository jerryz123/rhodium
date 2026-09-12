#!/usr/bin/env bash
# Builds and boots the actual MiniSoC through the native C runtime and coherent loader.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="${NATIVE_BUILD_DIR:-/tmp/rhodium-native-soc/mini}"
compiled_root="$(mktemp -d /tmp/rhodium-native-soc-compiled.XXXXXX)"
trap 'rm -rf "$compiled_root"' EXIT
mkdir -p "$build_dir"
build_dir="$(cd "$build_dir" && pwd)"
cd "$repo_dir"
# Release execution trusts partial-operation preconditions; debug retains checks.
native_mode_flags=(-DNDEBUG)
if [[ "${RDS_NATIVE_DEBUG:-0}" == 1 ]]; then native_mode_flags=(-UNDEBUG); fi
if [[ "${RDS_REUSE_MODEL:-0}" != 1 ]]; then
  if [[ "${RDS_REUSE_IR:-0}" != 1 ]]; then
    env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": RDS_MODEL="$build_dir/mini.rsim" \
      RDS_EXTRACT_ONLY=1 RDS_EXTRACTED="$build_dir/mini.extracted.json" \
      "${RACKET:-racket}" -y sims/native/emit-mini.rhm
  fi
  optimizer_args=()
  if [[ "${RDS_NATIVE_DEBUG:-0}" != 1 ]]; then optimizer_args+=(--release); fi
  if [[ "${RDS_REPLICATE:-0}" == 1 ]]; then
    optimizer_args+=(--replicate-bytes 65536 --replicate-work 2048)
  fi
  "${RDS_OPTIMIZER:-$repo_dir/rhodium/sim/compiler/run.sh}" --input "$build_dir/mini.extracted.json" \
    --output "$build_dir/mini.rsim" --report "$build_dir/mini.optimized.json" \
    --timing "$build_dir/compiler-timing.json" "${optimizer_args[@]}"
fi
"${CC:-cc}" -std=c17 -pthread ${RDS_NATIVE_CFLAGS:--O2} "${native_mode_flags[@]}" -g -Wall -Wextra -Werror ${RDS_SANITIZER_FLAGS:-} \
  sims/native/mini-smoke.c rhodium/sim/runtime/*.c -ldl -o "$build_dir/mini-smoke"
if [[ "${RDS_COMPILE_BLOCKS:-1}" == 1 ]] && (( (${RDS_FLAGS:-0} & 1) == 0 )); then
  "${CC:-cc}" -std=c17 -pthread ${RDS_NATIVE_CFLAGS:--O2} "${native_mode_flags[@]}" -Wall -Wextra -Werror \
    sims/native/compile-model.c rhodium/sim/runtime/*.c -ldl -o "$build_dir/compile-model"
  "$build_dir/compile-model" "$build_dir/mini.rsim" "$build_dir/blocks.c"
  "${CC:-cc}" -std=c17 ${RDS_NATIVE_CFLAGS:--O2} "${native_mode_flags[@]}" -fPIC -shared ${RDS_SANITIZER_FLAGS:-} "$build_dir/blocks.c" -o "$build_dir/blocks.so"
  export RDS_COMPILED="$build_dir/blocks.so"
fi
"$build_dir/mini-smoke" "$build_dir/mini.rsim" "${RDS_MAX_CYCLES:-200000}"
