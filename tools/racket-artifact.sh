#!/usr/bin/env bash
# Writes and verifies the exact-environment manifest for precompiled Rhodium bytecode.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiled_root="${PLTCOMPILEDROOTS:-}"
manifest_name="rhodium-bytecode-manifest"

if [[ -z "$compiled_root" || "$compiled_root" == *:* ]]; then
  echo "racket bytecode artifacts require exactly one compiled root" >&2
  exit 2
fi

manifest="$compiled_root/$manifest_name"
command="${1:-}"

manifest_value() {
  local key="$1"
  sed -n "s/^${key}=//p" "$manifest"
}

verify_value() {
  local key="$1"
  local expected="$2"
  local actual
  actual="$(manifest_value "$key")"
  if [[ "$actual" != "$expected" ]]; then
    echo "Rhodium bytecode manifest mismatch for $key: expected $expected, got $actual" >&2
    exit 1
  fi
}

case "$command" in
  write)
    commit="$(git -C "$repo_dir" rev-parse HEAD)"
    if [[ -n "${GITHUB_SHA:-}" && "$commit" != "$GITHUB_SHA" ]]; then
      echo "checked-out commit $commit does not match GITHUB_SHA $GITHUB_SHA" >&2
      exit 1
    fi
    if [[ "${GITHUB_ACTIONS:-}" == true ]] \
        && [[ -n "$(git -C "$repo_dir" status --porcelain)" ]]; then
      echo "refusing to publish bytecode from a dirty CI checkout" >&2
      exit 1
    fi
    mkdir -p "$compiled_root"
    cat > "$manifest" <<EOF
commit=$commit
racket=${RACKET_VERSION:-}
rhombus=${RHOMBUS_CHECKSUM:-}
os=$(uname -s)
architecture=$(uname -m)
workspace=$repo_dir
EOF
    ;;
  verify)
    if [[ ! -f "$manifest" ]]; then
      echo "missing Rhodium bytecode manifest: $manifest" >&2
      exit 1
    fi
    expected_commit="${GITHUB_SHA:-$(git -C "$repo_dir" rev-parse HEAD)}"
    verify_value commit "$expected_commit"
    verify_value racket "${RACKET_VERSION:-}"
    verify_value rhombus "${RHOMBUS_CHECKSUM:-}"
    verify_value os "$(uname -s)"
    verify_value architecture "$(uname -m)"
    verify_value workspace "$repo_dir"
    ;;
  *)
    echo "usage: $0 write|verify" >&2
    exit 2
    ;;
esac
