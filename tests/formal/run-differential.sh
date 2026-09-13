#!/usr/bin/env bash
# Replays Rosette models through the shared CIRCT and Verilator differential DUT.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
compiled_root="$(mktemp -d /tmp/rhodium-formal-differential-compiled.XXXXXX)"
model_file="$(mktemp /tmp/rhodium-formal-models.XXXXXX)"
trap 'rm -rf "$compiled_root" "$model_file"' EXIT

cd "$repo_dir"

if ! env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
    racket -y -S "$repo_dir" tests/formal/replay-models.rhm > "$model_file"; then
  echo 'formal-differential-test requires Rosette 4.0 and Z3 4.8.8; see rhodium/formal/README.md' >&2
  exit 1
fi

expected_keys=(
  SHIFT_VALUE SHIFT_AMOUNT SHIFT_REFERENCE SHIFT_DEFECT
  RECORD_HIGH RECORD_LOW RECORD_REFERENCE RECORD_DEFECT
  PAIR_LEFT PAIR_RIGHT PAIR_REFERENCE PAIR_DEFECT
  DECODE_SELECTOR DECODE_REFERENCE DECODE_DEFECT
  ONEHOT_SELECTOR ONEHOT_A ONEHOT_B ONEHOT_C
  ONEHOT_REFERENCE ONEHOT_DEFECT
  REACH_SELECTOR REACH_A REACH_B REACH_C REACH_OUTPUT
  PROPERTY_SELECTOR PROPERTY_A PROPERTY_B PROPERTY_C PROPERTY_OUTPUT
)
for key in "${expected_keys[@]}"; do
  if [[ "$(grep -Ec "^${key}=[0-9]+$" "$model_file")" != 1 ]]; then
    echo "formal model output must contain exactly one numeric $key assignment" >&2
    cat "$model_file" >&2
    exit 1
  fi
done
if [[ "$(wc -l < "$model_file" | tr -d ' ')" != "${#expected_keys[@]}" ]]; then
  echo 'formal model output contains unexpected lines' >&2
  cat "$model_file" >&2
  exit 1
fi

env FORMAL_REPLAY_FILE="$model_file" \
  PLTCOMPILEDROOTS="$compiled_root" \
  FIXTURE=formal-differential \
  bash tests/backend/run-circt.sh --simulate-only
