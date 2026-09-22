#!/usr/bin/env bash
# Manages persistent worktree-owned Racket bytecode with structural invalidation.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
racket_command="${RACKET:-racket}"
raco_command="${RACO:-raco}"
cache_format="v3"
lock_timeout="${RHODIUM_RACKET_CACHE_LOCK_TIMEOUT:-60}"
progress_interval="${RHODIUM_RACKET_CACHE_PROGRESS_INTERVAL:-30}"
held_locks=()
child_pid=""
monitor_pid=""
script_pid="$$"
script_parent_pid="$PPID"

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

cache_log() {
  if [[ "${RHODIUM_RACKET_CACHE_QUIET:-}" != 1 ]]; then
    printf 'rhodium-cache: %s\n' "$*" >&2
  fi
}

default_cache_dir() {
  printf '%s\n' "$repo_dir/.rhodium-cache/racket-build"
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
  local owner
  for lock in "${held_locks[@]-}"; do
    if [[ -n "$lock" ]]; then
      owner=""
      if [[ -r "$lock/pid" ]]; then
        read -r owner < "$lock/pid" || owner=""
      fi
      if [[ "$owner" == "$script_pid" ]]; then
        rm -rf -- "$lock"
      fi
    fi
  done
  held_locks=()
}

process_identity() {
  ps -p "$1" -o lstart= 2>/dev/null | awk '{$1=$1; print}' || true
}

lock_field() {
  local lock="$1"
  local field="$2"
  sed -n "s/^${field}=//p" "$lock/owner" 2>/dev/null | head -n 1 || true
}

acquire_lock() {
  local lock="$1"
  local operation="$2"
  local deadline=$((SECONDS + lock_timeout))
  local owner=""
  local owner_identity=""
  local current_identity=""
  local owner_operation=""
  local owner_started=""
  local waiting=false
  mkdir -p "$(dirname "$lock")"
  while ! mkdir "$lock" 2>/dev/null; do
    if [[ -r "$lock/pid" ]]; then
      read -r owner < "$lock/pid" || owner=""
    fi
    owner_identity="$(lock_field "$lock" identity)"
    current_identity=""
    if [[ "$owner" =~ ^[0-9]+$ ]] && kill -0 "$owner" 2>/dev/null; then
      current_identity="$(process_identity "$owner")"
    fi
    if [[ ! "$owner" =~ ^[0-9]+$ ]] || ! kill -0 "$owner" 2>/dev/null \
         || { [[ -n "$owner_identity" && -n "$current_identity" ]] \
              && [[ "$owner_identity" != "$current_identity" ]]; }; then
      rm -rf -- "$lock"
      owner=""
      continue
    fi
    owner_operation="$(lock_field "$lock" operation)"
    owner_started="$(lock_field "$lock" started)"
    if [[ "$waiting" == false ]]; then
      cache_log "waiting for worktree cache lock held by PID $owner (${owner_operation:-unknown operation}, started ${owner_started:-unknown})"
      waiting=true
    fi
    if (( SECONDS >= deadline )); then
      echo "Rhodium's worktree cache is busy: timed out after ${lock_timeout}s waiting for PID $owner (${owner_operation:-unknown operation}, started ${owner_started:-unknown})." >&2
      echo "Inspect it with: tools/racket-build-cache.sh status" >&2
      echo "The command was not retried with an uncached root." >&2
      exit 1
    fi
    sleep 0.1
  done
  printf '%s\n' "$script_pid" > "$lock/pid"
  {
    printf 'pid=%s\n' "$script_pid"
    printf 'parent_pid=%s\n' "$script_parent_pid"
    printf 'identity=%s\n' "$(process_identity "$script_pid")"
    printf 'started=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf 'operation=%s\n' "$operation"
    printf 'worktree=%s\n' "$repo_dir"
  } > "$lock/owner"
  held_locks+=("$lock")
}

cache_base="${RHODIUM_RACKET_CACHE_DIR:-$(default_cache_dir)}"
require_safe_directory "$cache_base"
if ! mkdir -p "$cache_base"; then
  echo "Rhodium cannot create this worktree's Racket cache at $cache_base" >&2
  echo "Set RHODIUM_RACKET_CACHE_DIR to one writable absolute directory; the command was not run uncached." >&2
  exit 1
fi
write_probe="$(mktemp "$cache_base/.write-probe.XXXXXX" 2>/dev/null || true)"
if [[ -z "$write_probe" ]]; then
  echo "Rhodium cannot write this worktree's Racket cache at $cache_base" >&2
  echo "Set RHODIUM_RACKET_CACHE_DIR to one writable absolute directory; the command was not run uncached." >&2
  exit 1
fi
rm -f -- "$write_probe"
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
  local operation="${1:-prepare}"
  cache_log "preparing worktree cache $compiled_root"
  acquire_lock "$workspace_lock" "$operation"
  prepare_locked
}

publish_dependencies_locked() {
  local dependency_lock="$dependency_entry.lock"
  local project_subtree="$compiled_root/${repo_dir#/}"
  local staging
  if [[ -f "$dependency_complete" && -d "$dependency_root" ]]; then
    return
  fi
  acquire_lock "$dependency_lock" "publish external dependencies"
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

terminate_child() {
  local attempt
  if [[ -z "$child_pid" ]] || ! kill -0 "$child_pid" 2>/dev/null; then
    return
  fi
  kill -TERM -- "-$child_pid" 2>/dev/null || kill -TERM "$child_pid" 2>/dev/null || true
  for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    if ! kill -0 "$child_pid" 2>/dev/null; then
      return
    fi
    sleep 0.1
  done
  kill -KILL -- "-$child_pid" 2>/dev/null || kill -KILL "$child_pid" 2>/dev/null || true
}

describe_command() {
  local argument
  local description=""
  local count=0
  if [[ "${1:-}" == env ]]; then
    shift
    while [[ $# -gt 0 && "$1" == *=* ]]; do
      shift
    done
  fi
  for argument in "$@"; do
    if [[ -n "$description" ]]; then
      description="$description "
    fi
    description="$description$argument"
    count=$((count + 1))
    if (( count == 3 )); then
      break
    fi
  done
  printf '%s\n' "${description:-unknown command}"
}

cleanup() {
  trap - EXIT HUP INT TERM
  if [[ -n "$monitor_pid" ]]; then
    kill "$monitor_pid" 2>/dev/null || true
  fi
  terminate_child
  release_locks
}

handle_signal() {
  exit "$1"
}

monitor_child() {
  local elapsed=0
  while kill -0 "$child_pid" 2>/dev/null; do
    sleep 1
    elapsed=$((elapsed + 1))
    if [[ "$script_parent_pid" =~ ^[0-9]+$ ]] && (( script_parent_pid > 1 )) \
       && ! kill -0 "$script_parent_pid" 2>/dev/null; then
      cache_log "invoking process exited; terminating cached command and releasing its worktree lock"
      kill -TERM -- "-$child_pid" 2>/dev/null || kill -TERM "$child_pid" 2>/dev/null || true
      kill -TERM "$script_pid" 2>/dev/null || true
      return
    fi
    if [[ "$progress_interval" =~ ^[0-9]+$ ]] && (( progress_interval > 0 )) \
       && (( elapsed % progress_interval == 0 )); then
      cache_log "command still running after ${elapsed}s (PID $child_pid)"
    fi
  done
}

run_command() {
  local description="$1"
  shift
  local status
  cache_log "running in the worktree cache: $description"
  set -m
  env PLTCOMPILEDROOTS="$compiled_root" "$@" &
  child_pid=$!
  set +m
  monitor_child &
  monitor_pid=$!
  set +e
  wait "$child_pid"
  status=$?
  set -e
  child_pid=""
  kill "$monitor_pid" 2>/dev/null || true
  wait "$monitor_pid" 2>/dev/null || true
  monitor_pid=""
  return "$status"
}

show_status() {
  local owner=""
  local owner_identity=""
  local current_identity=""
  printf 'worktree: %s\ncache: %s\ncompiled root: %s\n' "$repo_dir" "$cache_base" "$compiled_root"
  if [[ -r "$workspace_lock/pid" ]]; then
    read -r owner < "$workspace_lock/pid" || owner=""
    owner_identity="$(lock_field "$workspace_lock" identity)"
    if [[ "$owner" =~ ^[0-9]+$ ]] && kill -0 "$owner" 2>/dev/null; then
      current_identity="$(process_identity "$owner")"
    fi
    if [[ -n "$current_identity" && -n "$owner_identity" \
          && "$current_identity" != "$owner_identity" ]]; then
      printf 'lock: stale\n'
    elif [[ "$owner" =~ ^[0-9]+$ ]] && kill -0 "$owner" 2>/dev/null; then
      printf 'lock: held\n'
    else
      printf 'lock: stale\n'
    fi
    printf 'owner PID: %s\noperation: %s\nstarted: %s\n' \
      "${owner:-unknown}" \
      "$(lock_field "$workspace_lock" operation)" \
      "$(lock_field "$workspace_lock" started)"
  else
    printf 'lock: idle\n'
  fi
}

trap cleanup EXIT
trap 'handle_signal 129' HUP
trap 'handle_signal 130' INT
trap 'handle_signal 143' TERM

command="${1:-}"
case "$command" in
  path)
    [[ $# -eq 1 ]] || { echo "usage: $0 path" >&2; exit 2; }
    prepare "report cache path"
    printf '%s\n' "$compiled_root"
    ;;
  run)
    shift
    [[ $# -gt 0 ]] || { echo "usage: $0 run COMMAND [ARGUMENT ...]" >&2; exit 2; }
    description="$(describe_command "$@")"
    prepare "run $description"
    run_command "$description" "$@"
    ;;
  publish-dependencies)
    [[ $# -eq 1 ]] || { echo "usage: $0 publish-dependencies" >&2; exit 2; }
    prepare "publish external dependencies"
    publish_dependencies_locked
    ;;
  clean)
    [[ $# -eq 1 ]] || { echo "usage: $0 clean" >&2; exit 2; }
    acquire_lock "$workspace_lock" "clean cache"
    rm -rf -- "$workspace_entry"
    ;;
  status)
    [[ $# -eq 1 ]] || { echo "usage: $0 status" >&2; exit 2; }
    show_status
    ;;
  *)
    echo "usage: $0 path | run COMMAND [ARGUMENT ...] | publish-dependencies | clean | status" >&2
    exit 2
    ;;
esac
