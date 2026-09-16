#!/usr/bin/env bash
# Seeds fresh compiled roots from cached external Racket and Rhombus bytecode.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
racket_command="${RACKET:-racket}"
raco_command="${RACO:-raco}"
cache_format="v1"

hash_environment() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum
  else
    echo "Rhodium's Racket dependency cache requires shasum or sha256sum" >&2
    exit 1
  fi
}

default_cache_dir() {
  "$racket_command" -e \
    '(display (path->string (build-path (find-system-path (quote cache-dir)) "Rhodium" "racket-dependencies")))'
}

cache_key="$({
  printf '%s\n' "$cache_format" "$(uname -s)" "$(uname -m)"
  "$racket_command" --version
  COLUMNS=10000 "$raco_command" pkg show --all --long --full-checksum
} | hash_environment | awk '{print $1}')"
cache_base="${RHODIUM_RACKET_CACHE_DIR:-$(default_cache_dir)}"
cache_entry="$cache_base/$cache_format-$cache_key"
cache_root="$cache_entry/root"
complete_marker="$cache_entry/complete"

cache_is_complete() {
  [[ -f "$complete_marker" && -d "$cache_root" ]]
}

require_safe_root() {
  local root="$1"
  if [[ -z "$root" || "$root" == / || "$root" == *:* ]]; then
    echo "Rhodium dependency caching requires exactly one non-root compiled directory" >&2
    exit 2
  fi
}

seed_cache() {
  local target_root="$1"
  require_safe_root "$target_root"
  mkdir -p "$target_root"
  if ! cache_is_complete; then
    return 1
  fi
  cp -a "$cache_root"/. "$target_root"/
}

publish_cache() {
  local source_root="$1"
  local source_repo_dir="${2:-$repo_dir}"
  local lock_dir="$cache_entry.lock"
  local staging_dir=""

  require_safe_root "$source_root"
  if [[ ! -d "$source_root" || "$source_repo_dir" != /* || "$source_repo_dir" == / ]]; then
    echo "Rhodium dependency caching requires an existing root and an absolute repository path" >&2
    exit 2
  fi
  if cache_is_complete; then
    return
  fi

  mkdir -p "$cache_base"
  if ! mkdir "$lock_dir" 2>/dev/null; then
    if [[ -r "$lock_dir/pid" ]]; then
      read -r lock_pid < "$lock_dir/pid" || lock_pid=""
      if [[ "$lock_pid" =~ ^[0-9]+$ ]] && kill -0 "$lock_pid" 2>/dev/null; then
        return
      fi
    fi
    rm -rf "$lock_dir"
    if ! mkdir "$lock_dir" 2>/dev/null; then
      return
    fi
  fi
  printf '%s\n' "$$" > "$lock_dir/pid"

  cleanup_publish() {
    if [[ -n "$staging_dir" && -d "$staging_dir" ]]; then
      rm -rf "$staging_dir"
    fi
    rm -rf "$lock_dir"
  }
  trap cleanup_publish EXIT

  if cache_is_complete; then
    cleanup_publish
    trap - EXIT
    return
  fi
  rm -rf "$cache_entry"
  staging_dir="$(mktemp -d "$cache_base/.rhodium-racket-cache.XXXXXX")"
  mkdir -p "$staging_dir/root"
  cp -a "$source_root"/. "$staging_dir/root"/
  rm -rf "$staging_dir/root/${source_repo_dir#/}"
  printf 'format=%s\nkey=%s\n' "$cache_format" "$cache_key" > "$staging_dir/complete"
  mv "$staging_dir" "$cache_entry"
  staging_dir=""
  cleanup_publish
  trap - EXIT
}

case "${1:-}" in
  seed)
    [[ $# -eq 2 ]] || { echo "usage: $0 seed COMPILED_ROOT" >&2; exit 2; }
    seed_cache "$2"
    ;;
  publish)
    [[ $# -ge 2 && $# -le 3 ]] \
      || { echo "usage: $0 publish COMPILED_ROOT [REPOSITORY]" >&2; exit 2; }
    publish_cache "$2" "${3:-$repo_dir}"
    ;;
  path)
    [[ $# -eq 1 ]] || { echo "usage: $0 path" >&2; exit 2; }
    printf '%s\n' "$cache_entry"
    ;;
  *)
    echo "usage: $0 seed COMPILED_ROOT | publish COMPILED_ROOT [REPOSITORY] | path" >&2
    exit 2
    ;;
esac
