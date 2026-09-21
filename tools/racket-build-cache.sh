#!/usr/bin/env bash
# Manages persistent worktree-local Racket bytecode with structural invalidation.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
racket_command="${RACKET:-racket}"
raco_command="${RACO:-raco}"
cache_format="v2"
lock_timeout="${RHODIUM_RACKET_CACHE_LOCK_TIMEOUT:-60}"
held_locks=()

hash_stream() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 | awk '{print $1}'
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum | awk '{print $1}'
  else
    echo "Rhodium's Racket build cache requires shasum or sha256sum" >&2
    exit 1
  fi
}

default_cache_dir() {
  "$racket_command" -e \
    '(display (path->string (build-path (find-system-path (quote cache-dir)) "Rhodium" "racket-build")))'
}

require_safe_directory() {
  local directory="$1"
  if [[ -z "$directory" || "$directory" != /* || "$directory" == / || "$directory" == *:* ]]; then
    echo "Rhodium's Racket build cache requires one absolute non-root directory" >&2
    exit 2
  fi
}

release_locks() {
  local lock
  for lock in "${held_locks[@]-}"; do
    if [[ -n "$lock" ]]; then
      rm -rf -- "$lock"
    fi
  done
  held_locks=()
}

acquire_lock() {
  local lock="$1"
  local deadline=$((SECONDS + lock_timeout))
  local owner=""
  mkdir -p "$(dirname "$lock")"
  while ! mkdir "$lock" 2>/dev/null; do
    if [[ -r "$lock/pid" ]]; then
      read -r owner < "$lock/pid" || owner=""
    fi
    if [[ ! "$owner" =~ ^[0-9]+$ ]] || ! kill -0 "$owner" 2>/dev/null; then
      rm -rf -- "$lock"
      owner=""
      continue
    fi
    if (( SECONDS >= deadline )); then
      echo "timed out waiting for Racket build cache lock held by process $owner" >&2
      exit 1
    fi
    sleep 0.1
  done
  printf '%s\n' "$$" > "$lock/pid"
  held_locks+=("$lock")
  trap release_locks EXIT HUP INT TERM
}

cache_base="${RHODIUM_RACKET_CACHE_DIR:-$(default_cache_dir)}"
require_safe_directory "$cache_base"
package_state_key="$({
  printf '%s\n' "$cache_format" "$(uname -s)" "$(uname -m)"
  "$racket_command" --version
  while IFS= read -r package_directory; do
    printf 'directory=%s\n' "$package_directory"
    if [[ -d "$package_directory" ]]; then
      find "$package_directory" "$(dirname "$package_directory")" -maxdepth 1 \
        -type f \( -name '*.rktd' -o -name 'config.rktd' \) ! -name '.LOCK*' -print 2>/dev/null \
        | LC_ALL=C sort \
        | while IFS= read -r metadata; do
            printf 'metadata=%s\n' "$metadata"
            hash_stream < "$metadata"
          done
    else
      printf 'missing\n'
    fi
  done < <("$racket_command" -e \
    '(require setup/dirs) (for ([p (in-list (list (find-config-dir) (find-pkgs-dir) (find-user-pkgs-dir)))]) (displayln (path->string p)))')
} | hash_stream)"
environment_memo="$cache_base/environments/$package_state_key"
if [[ -f "$environment_memo" ]]; then
  read -r environment_key < "$environment_memo"
else
  mkdir -p "$(dirname "$environment_memo")"
  environment_key="$({
    printf '%s\n' "$cache_format" "$(uname -s)" "$(uname -m)"
    "$racket_command" --version
    COLUMNS=10000 "$raco_command" pkg show --all --long --full-checksum
  } | hash_stream)"
  environment_memo_tmp="$(mktemp "$(dirname "$environment_memo")/.environment.XXXXXX")"
  printf '%s\n' "$environment_key" > "$environment_memo_tmp"
  mv "$environment_memo_tmp" "$environment_memo"
fi
workspace_key="$(printf '%s\n' "$repo_dir" | hash_stream)"
dependency_entry="$cache_base/dependencies/$environment_key"
dependency_root="$dependency_entry/root"
dependency_complete="$dependency_entry/complete"
workspace_entry="$cache_base/workspaces/$environment_key/$workspace_key"
compiled_root="$workspace_entry/root"
source_manifest="$workspace_entry/source-paths"
content_manifest="$workspace_entry/source-content"
workspace_lock="$workspace_entry.lock"

write_source_manifest() {
  git -C "$repo_dir" ls-files --cached --others --exclude-standard -- \
    '*.rkt' '*.rhm' '*.rhdl' | LC_ALL=C sort -u
}

write_content_manifest() {
  local paths="$1"
  local hashes
  hashes="$(mktemp "$workspace_entry/.source-hashes.XXXXXX")"
  git -C "$repo_dir" hash-object --stdin-paths < "$paths" > "$hashes"
  paste "$paths" "$hashes"
  rm -f -- "$hashes"
}

prepare_locked() {
  local next_manifest
  local next_content
  local changed_sources
  local changed_arguments=()
  local project_subtree="$compiled_root/${repo_dir#/}"
  require_safe_directory "$compiled_root"
  require_safe_directory "$project_subtree"
  mkdir -p "$workspace_entry"
  next_manifest="$(mktemp "$workspace_entry/.source-paths.XXXXXX")"
  next_content="$(mktemp "$workspace_entry/.source-content.XXXXXX")"
  write_source_manifest > "$next_manifest"
  write_content_manifest "$next_manifest" > "$next_content"

  if [[ ! -d "$compiled_root" ]]; then
    mkdir -p "$compiled_root"
    if [[ -f "$dependency_complete" && -d "$dependency_root" ]]; then
      cp -a "$dependency_root"/. "$compiled_root"/
    fi
  elif [[ ! -f "$source_manifest" ]] || ! cmp -s "$source_manifest" "$next_manifest"; then
    # A moved or deleted source can leave loadable orphan bytecode behind. Clear
    # only this checkout's mirrored subtree and retain compiled dependencies.
    rm -rf -- "$project_subtree"
  elif [[ ! -f "$content_manifest" ]] || ! cmp -s "$content_manifest" "$next_content"; then
    changed_sources="$(mktemp "$workspace_entry/.changed-sources.XXXXXX")"
    awk -F '\t' 'NR == FNR { previous[$1] = $2; next }
      previous[$1] != $2 { print $1 }' "$content_manifest" "$next_content" \
      > "$changed_sources"
    if [[ -s "$changed_sources" && -d "$project_subtree" ]]; then
      while IFS= read -r changed_source; do
        changed_arguments+=("$repo_dir/$changed_source")
      done < "$changed_sources"
      env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
        "$racket_command" "$repo_dir/tools/invalidate-racket-build-cache.rkt" \
        "$repo_dir" "$next_manifest" "${changed_arguments[@]}"
    fi
    rm -f -- "$changed_sources"
  fi

  mv "$next_manifest" "$source_manifest"
  mv "$next_content" "$content_manifest"
  printf '%s\n' "$repo_dir" > "$workspace_entry/workspace"
}

prepare() {
  acquire_lock "$workspace_lock"
  prepare_locked
}

publish_dependencies_locked() {
  local dependency_lock="$dependency_entry.lock"
  local project_subtree="$compiled_root/${repo_dir#/}"
  local staging
  if [[ -f "$dependency_complete" && -d "$dependency_root" ]]; then
    return
  fi
  acquire_lock "$dependency_lock"
  if [[ -f "$dependency_complete" && -d "$dependency_root" ]]; then
    return
  fi
  mkdir -p "$(dirname "$dependency_entry")"
  staging="$(mktemp -d "$(dirname "$dependency_entry")/.dependencies.XXXXXX")"
  mkdir -p "$staging/root"
  cp -a "$compiled_root"/. "$staging/root"/
  rm -rf -- "$staging/root/${project_subtree#$compiled_root/}"
  printf 'format=%s\nenvironment=%s\n' "$cache_format" "$environment_key" > "$staging/complete"
  rm -rf -- "$dependency_entry"
  mv "$staging" "$dependency_entry"
}

command="${1:-}"
case "$command" in
  path)
    [[ $# -eq 1 ]] || { echo "usage: $0 path" >&2; exit 2; }
    prepare
    printf '%s\n' "$compiled_root"
    ;;
  run)
    shift
    [[ $# -gt 0 ]] || { echo "usage: $0 run COMMAND [ARGUMENT ...]" >&2; exit 2; }
    prepare
    set +e
    env PLTCOMPILEDROOTS="$compiled_root" "$@"
    status=$?
    set -e
    exit "$status"
    ;;
  publish-dependencies)
    [[ $# -eq 1 ]] || { echo "usage: $0 publish-dependencies" >&2; exit 2; }
    prepare
    publish_dependencies_locked
    ;;
  clean)
    [[ $# -eq 1 ]] || { echo "usage: $0 clean" >&2; exit 2; }
    acquire_lock "$workspace_lock"
    rm -rf -- "$workspace_entry"
    ;;
  *)
    echo "usage: $0 path | run COMMAND [ARGUMENT ...] | publish-dependencies | clean" >&2
    exit 2
    ;;
esac
