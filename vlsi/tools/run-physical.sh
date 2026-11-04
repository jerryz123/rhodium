#!/usr/bin/env bash
# Runs one physical-tool stage, entering the pinned Nix environment only when its tool is absent.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
stage="${1:?physical stage required}"
tool=librelane
case "$stage" in post-synth) tool="${YOSYS:-yosys}";; integrate) tool=magic;; esac
if ! command -v "$tool" >/dev/null; then
  [[ ${RHODIUM_PHYSICAL_ENV:-0} != 1 ]] || { echo "Tool missing in the pinned physical environment: $tool" >&2; exit 2; }
  nix="${NIX:-/nix/var/nix/profiles/default/bin/nix}"
  if [[ ! -x $nix ]]; then nix=nix; fi
  export RHODIUM_PHYSICAL_ENV=1
  exec "$nix" --extra-experimental-features 'nix-command flakes' --accept-flake-config \
    develop "$PWD/vlsi/double_wide_openframe" --command bash "$0" "$stage"
fi
export PDK_ROOT="${PDK_ROOT:-$HOME/.ciel}" PDK="${PDK:-sky130A}"
lane=(librelane --pdk-root "$PDK_ROOT" --pdk "$PDK")
mini="$PWD/vlsi/openlane/mini_rv5stage_soc"
case "$stage" in
  slang|synth)
    tag=RHODIUM_SLANG_SYNTH
    extra=()
    if [[ $stage == slang ]]; then tag=RHODIUM_SLANG_CHECK; extra=(--override-config SYNTH_ELABORATE_ONLY=true); fi
    "${lane[@]}" "$mini/config.yaml" --run-tag "$tag" --overwrite "${extra[@]}" --only Yosys.Synthesis --condensed --hide-progress-bar
    netlist="$mini/runs/$tag/final/nl/MiniRV5StageSoC.nl.v"
    test -s "$netlist"
    grep -q use_slang "$mini/runs/$tag"/*-yosys-synthesis/yosys-synthesis.log
    if [[ $stage == slang ]]; then
      test "$(grep -c 'sky130_sram_2kbyte_1rw1r_32x512_8 ' "$netlist")" -eq 36
      ! grep -Eq 'struct[[:space:]]+packed|typedef[[:space:]]+struct' "$netlist"
    fi ;;
  post-synth)
    export MINI_RV5STAGE_SOC_SYNTH_STEP_DIR="${MINI_RV5STAGE_SOC_SYNTH_STEP_DIR:-$mini/runs/RHODIUM_SLANG_SYNTH/01-yosys-synthesis}"
    export MINI_RV5STAGE_SOC_POST_SYNTH_DIR="${MINI_RV5STAGE_SOC_POST_SYNTH_DIR:-$PWD/vlsi/build/mini-rv5stage-soc/synthesis}"
    export MINI_RV5STAGE_SOC_REPAIR_MULTIPLIER="${MINI_RV5STAGE_SOC_REPAIR_MULTIPLIER:-0}"
    mkdir -p "$MINI_RV5STAGE_SOC_POST_SYNTH_DIR"
    "$tool" -Q -T -l "$MINI_RV5STAGE_SOC_POST_SYNTH_DIR/yosys.log" -c "$mini/post-synth.tcl"
    test "$(grep -c 'sky130_sram_2kbyte_1rw1r_32x512_8 ' "$MINI_RV5STAGE_SOC_POST_SYNTH_DIR/MiniRV5StageSoC.nl.v")" -eq 36 ;;
  harden)
    "${lane[@]}" vlsi/openlane/double_wide_openframe_project_wrapper/config.yaml --save-views-to "$PWD/vlsi/build/views"
    test -s vlsi/build/views/gds/double_wide_openframe_project_wrapper.gds ;;
  lvs)
    "${lane[@]}" vlsi/openlane/rhodium_lvs_smoke/config.yaml --run-tag RHODIUM_LVS --overwrite --condensed --hide-progress-bar --save-views-to "$PWD/vlsi/build/lvs-views"
    test -s vlsi/build/lvs-views/gds/rhodium_lvs_smoke.gds
    grep -Eq '^Final result: (Circuits|Netlists) match' vlsi/openlane/rhodium_lvs_smoke/runs/RHODIUM_LVS/*-netgen-lvs/reports/lvs.netgen.rpt ;;
  integrate)
    mkdir -p vlsi/build/integrated
    export PADFRAME_GDS="$PWD/vlsi/double_wide_openframe/gds/double_wide_chip_io.gds"
    export USER_WRAPPER_GDS="$PWD/vlsi/build/views/gds/double_wide_openframe_project_wrapper.gds"
    export OUT_GDS="$PWD/vlsi/build/integrated/double_wide_openframe.gds"
    magic -dnull -noconsole -rcfile "$PDK_ROOT/$PDK/libs.tech/magic/$PDK.magicrc" vlsi/double_wide_openframe/scripts/integrate_openframe.tcl
    test -s "$OUT_GDS" ;;
  *) echo "Unknown physical stage: $stage" >&2; exit 2 ;;
esac
