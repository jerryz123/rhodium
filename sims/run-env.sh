#!/usr/bin/env bash
# Resolves a Moon-selected simulator and verifies its identity before execution.
# SPDX-License-Identifier: Apache-2.0
# Source from the repository root. Selection comes from the Moon task, not a
# second CLI parser; caller overrides select artifact locations only.
set -eo pipefail
if [[ -z ${RHODIUM_SOC:-} ]]; then
  selection=(--soc "${SOC:-simple}" --isa "${ISA:-}")
  if [[ -n ${CORE:-} ]]; then selection+=(--core "$CORE"); fi
  axes=$(python3 socs/configs/selection.py "${selection[@]}")
  read -r shape core isa <<< "$axes"
  RHODIUM_SOC="$shape-$core-$isa"
fi
RHODIUM_BACKEND="${RHODIUM_BACKEND:-${RTL_BACKEND:-rsim}}"
if [[ -z ${RHODIUM_VARIANT:-} ]]; then
  RHODIUM_VARIANT=normal
  if [[ ${TRACE:-0} == 1 ]]; then RHODIUM_VARIANT=trace; fi
  if [[ ${COSIM:-0} == 1 ]]; then
    RHODIUM_VARIANT="${RHODIUM_VARIANT/normal/}"
    RHODIUM_VARIANT="${RHODIUM_VARIANT:+$RHODIUM_VARIANT-}cosim"
  fi
  if [[ ${STATS:-0} == 1 ]]; then RHODIUM_VARIANT+=-stats; fi
fi
sim_dir="${BUILD_ROOT:-/tmp/rhodium-sims}/$RHODIUM_SOC"
payload_dir="$PWD/.rhodium-cache/moon/payloads/$RHODIUM_SOC/$RHODIUM_VARIANT"
if [[ $RHODIUM_BACKEND != rsim ]]; then sim_dir+="-$RHODIUM_BACKEND"; fi
if [[ $RHODIUM_VARIANT != normal ]]; then sim_dir+="-${RHODIUM_VARIANT#normal-}"; fi
simulator="${PREBUILT_SIMULATOR:-$sim_dir/obj/VTestDriver}"
simulator="$(cd "$(dirname "$simulator")" && pwd)/$(basename "$simulator")"
target="${PREBUILT_PROGRAM_TARGET:-${PROGRAM_TARGET:-$(dirname "$simulator")/program-target.json}}"
python3 sims/program-test/artifact.py verify --binary "$simulator" \
  --soc "$RHODIUM_SOC" --target "$target" \
  --backend "$RHODIUM_BACKEND" --variant "$RHODIUM_VARIANT"
trace_args=()
uart_args=()
if [[ -n ${UART_LOG:-} ]]; then uart_args=("+uart-log=$UART_LOG"); fi
if [[ $RHODIUM_VARIANT == trace* ]]; then
  : "${TRACE_FILE:?TRACE_FILE is required for a traced simulator}"
  trace_args=("+rheg-trace=$TRACE_FILE")
fi
# These public options are whitespace-delimited argument lists, as in the
# former Make interface. Arrays prevent glob expansion and command evaluation.
read -r -a htif_args <<< "${HTIF_ARGS:-}"
read -r -a target_args <<< "${TARGET_ARGS:-}"
