#!/usr/bin/env bash
# Resolves native toolchain paths shared by Moon compilation leaves, without scheduling builds.
# SPDX-License-Identifier: Apache-2.0
set -eo pipefail
repo_dir="$PWD"
spike_identity=$(python3 riscv/patched_submodule.py identity --repository "$PWD" --submodule riscv/riscv-isa-sim --series riscv/riscv-isa-sim-patches/series)
fesvr_prefix="${FESVR_PREFIX:-$PWD/.tools/fesvr-$spike_identity}"
native_dir="$PWD/.rhodium-cache/moon/native"
mkdir -p "$native_dir"
read -r -a cppflags <<< "${CPPFLAGS:-}"
source "$repo_dir/tools/build/paths.sh"
compilation_path_flags --build "$native_dir" --map "$fesvr_prefix=/rhodium/fesvr"
cppflags+=("${path_flags[@]}")
read -r -a cxxflags <<< "${CXXFLAGS:--O2}"
read -r -a ldflags <<< "${LDFLAGS:-}"
read -r -a ldlibs <<< "${LDLIBS:-}"
native_includes=(-I"$fesvr_prefix/include" -I"$PWD/sims/fesvr" -I"$PWD/sims/verilator" -I"$PWD/chi/subordinate/dpi")
spike_includes=(-I"$PWD/cores/spike/dpi" -I"$fesvr_prefix/include/riscv" -iquote "$fesvr_prefix/include/fesvr")
spike_libs=(-L"$fesvr_prefix/lib" -Wl,-rpath,"$fesvr_prefix/lib" -lriscv -lfesvr -ldisasm -lsoftfloat -pthread)
