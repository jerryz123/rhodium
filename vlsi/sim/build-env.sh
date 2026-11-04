#!/usr/bin/env bash
# Resolves the supported mapped simulator and keeps compiler work paths separate from cached inputs.
# SPDX-License-Identifier: Apache-2.0
set -eo pipefail
[[ ${SOC:-mini-rv5stage-soc} == mini-rv5stage-soc ]] || { echo 'Mapped simulation requires SOC=mini-rv5stage-soc' >&2; exit 2; }
[[ ${TECH:-sky130} == sky130 ]] || { echo 'Mapped simulation requires TECH=sky130' >&2; exit 2; }
mapped_source="$PWD/vlsi/build/sim/mini-rv5stage-soc/sky130"
build="${BUILD_ROOT:-$PWD/vlsi/build/sim}/mini-rv5stage-soc/sky130"
case "$build" in /*) ;; *) build="$PWD/$build";; esac
