#!/usr/bin/env bash
# Resolves local selectors to concrete Moon build targets; does not implement a build graph.
# SPDX-License-Identifier: Apache-2.0
set -eo pipefail
kind="${1:?Moon task family required}"
source sims/build-env.sh
case "$kind" in
  target) task="target--$soc" ;;
  simulator) task="publish--$soc--$backend--$variant" ;;
  model) task="model--$soc--$backend--$variant" ;;
  act) task="act-elf--${ACT_CONFIGURATION:-$soc}" ;;
  act-config) task="act-config--${ACT_CONFIGURATION:-$soc}" ;;
  *) echo "Unknown local build family: $kind" >&2; exit 2 ;;
esac
options=()
if [[ -n ${PREBUILT_SIMULATOR:-} ]]; then options=(--upstream none); fi
exec moon run "rhodium:$task" "${options[@]}"
