#!/usr/bin/env bash
# Compiles one emitted model using Verilator's generated native build, retaining warm objects.
# SPDX-License-Identifier: Apache-2.0
source sims/build-env.sh
source tools/build/native-env.sh
mkdir -p "$work_dir/obj"
sources=("$work_dir/SoCHarness.sv" "$PWD/sims/TestDriver.v" "$PWD/sims/fesvr/direct_mem_htif.cc" "$PWD/sims/verilator/direct_mem_htif_dpi.cc" "$PWD/devices/uart/dpi/uart_dpi.cc" "$PWD/sims/verilator/image_memory_map.cc" "$PWD/sims/verilator/simulation_runtime.cc")
compilation_path_flags --build "$work_dir" --map "$fesvr_prefix=/rhodium/fesvr"
includes=("${native_includes[@]}" -std=c++20)
libraries=("$fesvr_prefix/lib/libfesvr.a" -pthread)
files=(SoCHarness.sv)
warnings=()
if [[ $backend == rsim ]]; then
  files+=(SoCHarness.cpp SoCHarness_bridge.cpp SoCHarness.hpp)
  if [[ -f "$model_dir/rsim-bits.hpp" ]]; then files+=(rsim-bits.hpp); fi
  sources+=("$work_dir/SoCHarness.cpp" "$work_dir/SoCHarness_bridge.cpp")
elif [[ $backend == verilog ]]; then warnings+=(--Wno-UNSIGNED --Wno-CMPCONST); fi
if [[ $shape != mini ]]; then
  sources+=("$PWD/chi/subordinate/dpi/chi_dpi_memory_dpi.cc" "$PWD/chi/subordinate/dpi/chi_memory.cc")
  includes+=(-DRHODIUM_CHI_MEMORY)
fi
if [[ $core == spike ]]; then
  sources+=("$PWD/cores/spike/dpi/spike_core.cc" "$PWD/cores/spike/dpi/spike_dpi.cc")
  includes+=("${spike_includes[@]}")
  libraries+=("${spike_libs[@]}")
fi
if [[ ${TRACE:-0} == 1 ]]; then
  files+=(soc_events.h soc_events.json)
  sources+=("$PWD/sims/verilator/event_trace.cc")
  includes+=(-DRHEG_TRACE -I"$work_dir" -I"$PWD/rheg/runtime" -I"$PWD/rheg/perfetto")
  libraries+=("$PWD/.rhodium-cache/moon/trace/librheg_perfetto.a" "$PWD/.rhodium-cache/moon/trace/librheg_runtime.a" -lz)
fi
if [[ ${COSIM:-0} == 1 ]]; then
  files+=(soc_cosim.json)
  python3 sims/cosim/runtime/artifact.py verify "$cosim_build"
  sources+=("$PWD/sims/cosim/runtime/session.cc")
  includes+=(-DRHODIUM_COSIM -I"$PWD/sims/cosim" -I"$cosim_config" -I"$sail_prefix/include/sail-model")
  read -r -a cosim_links < "$cosim_build/link-flags.txt"
  libraries+=("${cosim_links[@]}")
fi
if [[ ${STATS:-0} == 1 ]]; then
  files+=(soc_statistics.h soc_statistics.json)
  sources+=("$PWD/sims/statistics/collector.cc")
  includes+=(-DRHODIUM_STATS -I"$work_dir")
fi
for file in "${files[@]}"; do copy_changed "$model_dir/$file" "$work_dir/$file"; done
read -r -a verilator <<< "${VERILATOR:-verilator}"
"${verilator[@]}" --binary --timing --vpi --assert --Wno-UNOPTFLAT --Wno-SYMRSVDWORD "${warnings[@]}" --build-jobs "${BUILD_JOBS:-4}" \
  -MAKEFLAGS "-f $PWD/sims/verilator/relink.mk OPT_FAST='${OPT_FAST:--O2}' OPT_SLOW='${OPT_SLOW:-${OPT_FAST:--O2}}' OPT_GLOBAL='${OPT_GLOBAL:-${OPT_FAST:--O2}}'" \
  --top-module TestDriver --Mdir "$work_dir/obj" "${sources[@]}" \
  -CFLAGS "$path_flags_shell ${includes[*]}" -LDFLAGS "${libraries[*]}"
copy_changed "$work_dir/obj/VTestDriver" "$model_dir/VTestDriver"
