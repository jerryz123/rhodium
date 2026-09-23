#!/usr/bin/env bash
# Verifies worktree isolation, interruption recovery, reuse, invalidation, and cleanup.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fixture_dir="$(mktemp -d /tmp/rhodium-racket-build-cache-test.XXXXXX)"
fixture_repo="$fixture_dir/repository"
second_fixture_repo="$fixture_dir/second-repository"
probe_source="$fixture_repo/probe.rkt"
dependency_source="$fixture_repo/dependency.rkt"
importer_source="$fixture_repo/importer.rkt"
runner_source="$fixture_repo/runner.rkt"
real_racket="$(command -v racket)"
real_raco="$(command -v raco)"
cache_process_pid=""

cleanup() {
  if [[ -n "$cache_process_pid" ]]; then
    kill -TERM "$cache_process_pid" 2>/dev/null || true
    wait "$cache_process_pid" 2>/dev/null || true
  fi
  rm -rf -- "$fixture_dir"
}
trap cleanup EXIT

mkdir -p "$fixture_dir/packages" "$fixture_repo/tools" "$second_fixture_repo/tools"
cp "$repo_dir/tools/racket-build-cache.sh" "$fixture_repo/tools/racket-build-cache.sh"
cp "$repo_dir/tools/invalidate-racket-build-cache.rkt" \
  "$fixture_repo/tools/invalidate-racket-build-cache.rkt"
cp "$repo_dir/tools/racket-build-cache.sh" "$second_fixture_repo/tools/racket-build-cache.sh"
cp "$repo_dir/tools/invalidate-racket-build-cache.rkt" \
  "$second_fixture_repo/tools/invalidate-racket-build-cache.rkt"
git -C "$fixture_repo" init -q
git -C "$second_fixture_repo" init -q
cat > "$fixture_dir/fake-racket" <<'EOF'
#!/usr/bin/env bash
# Supplies deterministic Racket metadata to the build-cache test.
set -euo pipefail
if [[ "${1:-}" == --version ]]; then
  echo 'Racket test version'
elif [[ "${1:-}" == -e ]]; then
  printf '%s\n' "$FAKE_PACKAGE_DIR" "$FAKE_PACKAGE_DIR" "$FAKE_PACKAGE_DIR"
else
  exec "$REAL_RACKET" "$@"
fi
EOF
cat > "$fixture_dir/fake-raco" <<'EOF'
#!/usr/bin/env bash
# Supplies deterministic package metadata to the build-cache test.
set -euo pipefail
if [[ "${1:-}" == pkg && "${2:-}" == show ]]; then
  echo 'test-package checksum test-source'
else
  echo "unexpected fake raco invocation: $*" >&2
  exit 1
fi
EOF
chmod +x "$fixture_dir/fake-racket" "$fixture_dir/fake-raco"

cache_command=(env
  RHODIUM_RACKET_CACHE_DIR="$fixture_dir/cache"
  RHODIUM_RACKET_CACHE_QUIET=1
  RACKET="$fixture_dir/fake-racket"
  RACO="$fixture_dir/fake-raco"
  FAKE_PACKAGE_DIR="$fixture_dir/packages"
  REAL_RACKET="$real_racket"
  "$fixture_repo/tools/racket-build-cache.sh")
second_cache_command=(env
  RHODIUM_RACKET_CACHE_DIR="$fixture_dir/cache"
  RHODIUM_RACKET_CACHE_QUIET=1
  RACKET="$fixture_dir/fake-racket"
  RACO="$fixture_dir/fake-raco"
  FAKE_PACKAGE_DIR="$fixture_dir/packages"
  REAL_RACKET="$real_racket"
  "$second_fixture_repo/tools/racket-build-cache.sh")
default_cache_command=(env
  RHODIUM_RACKET_CACHE_QUIET=1
  RACKET="$fixture_dir/fake-racket"
  RACO="$fixture_dir/fake-raco"
  FAKE_PACKAGE_DIR="$fixture_dir/packages"
  REAL_RACKET="$real_racket"
  "$fixture_repo/tools/racket-build-cache.sh")

compiled_root="$("${cache_command[@]}" path)"
[[ -d "$compiled_root" ]]
[[ "$("${cache_command[@]}" path)" == "$compiled_root" ]]
[[ "$("${cache_command[@]}" run bash -c 'printf %s "$PLTCOMPILEDROOTS"')" == "$compiled_root" ]]
default_compiled_root="$("${default_cache_command[@]}" path)"
[[ "$default_compiled_root" == "$fixture_repo/.rhodium-cache/racket-build/"* ]]
second_compiled_root="$("${second_cache_command[@]}" path)"
[[ "$second_compiled_root" != "$compiled_root" ]]
[[ "$second_compiled_root" == "$fixture_dir/cache/"* ]]

command_marker="$fixture_dir/uncached-command-ran"
touch "$fixture_dir/not-a-directory"
if env RHODIUM_RACKET_CACHE_DIR="$fixture_dir/not-a-directory" \
       RHODIUM_RACKET_CACHE_QUIET=1 \
       RACKET="$fixture_dir/fake-racket" RACO="$fixture_dir/fake-raco" \
       FAKE_PACKAGE_DIR="$fixture_dir/packages" REAL_RACKET="$real_racket" \
       "$fixture_repo/tools/racket-build-cache.sh" run touch "$command_marker" \
       >"$fixture_dir/unwritable.out" 2>"$fixture_dir/unwritable.err"; then
  echo "unwritable cache unexpectedly ran its command" >&2
  exit 1
fi
[[ ! -e "$command_marker" ]]
grep -q 'command was not run uncached' "$fixture_dir/unwritable.err"

workspace_entry="${compiled_root%/root}"
workspace_lock="$workspace_entry.lock"
"${cache_command[@]}" run bash -c 'trap "" TERM; while :; do sleep 1; done' \
  >"$fixture_dir/interrupted.out" 2>"$fixture_dir/interrupted.err" &
cache_process_pid=$!
for _attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
  [[ -d "$workspace_lock" ]] && break
  sleep 0.1
done
[[ -d "$workspace_lock" ]]
status_output="$("${cache_command[@]}" status)"
[[ "$status_output" == *'lock: held'* ]]
[[ "$status_output" == *"owner PID: $cache_process_pid"* ]]

if env RHODIUM_RACKET_CACHE_DIR="$fixture_dir/cache" \
       RHODIUM_RACKET_CACHE_QUIET=1 RHODIUM_RACKET_CACHE_LOCK_TIMEOUT=1 \
       RACKET="$fixture_dir/fake-racket" RACO="$fixture_dir/fake-raco" \
       FAKE_PACKAGE_DIR="$fixture_dir/packages" REAL_RACKET="$real_racket" \
       "$fixture_repo/tools/racket-build-cache.sh" run touch "$command_marker" \
       >"$fixture_dir/busy.out" 2>"$fixture_dir/busy.err"; then
  echo "busy cache unexpectedly ran its command" >&2
  exit 1
fi
[[ ! -e "$command_marker" ]]
grep -q 'The command was not retried with an uncached root' "$fixture_dir/busy.err"

kill -TERM "$cache_process_pid"
if wait "$cache_process_pid"; then
  echo "interrupted cache command unexpectedly succeeded" >&2
  exit 1
fi
cache_process_pid=""
for _attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do
  [[ ! -d "$workspace_lock" ]] && break
  sleep 0.1
done
[[ ! -d "$workspace_lock" ]]
[[ "$("${cache_command[@]}" run bash -c 'printf recovered')" == recovered ]]

mkdir "$workspace_lock"
printf '%s\n' 2147483647 > "$workspace_lock/pid"
{
  printf 'pid=2147483647\n'
  printf 'identity=unreachable process\n'
  printf 'started=1970-01-01T00:00:00Z\n'
  printf 'operation=abandoned test command\n'
} > "$workspace_lock/owner"
stale_status="$("${cache_command[@]}" status)"
[[ "$stale_status" == *'lock: stale'* ]]
"${cache_command[@]}" path >/dev/null
[[ ! -d "$workspace_lock" ]]

env RHODIUM_RACKET_CACHE_DIR="$fixture_dir/cache" \
    RHODIUM_RACKET_CACHE_PROGRESS_INTERVAL=1 \
    RACKET="$fixture_dir/fake-racket" RACO="$fixture_dir/fake-raco" \
    FAKE_PACKAGE_DIR="$fixture_dir/packages" REAL_RACKET="$real_racket" \
    "$fixture_repo/tools/racket-build-cache.sh" run sleep 2 \
    >"$fixture_dir/progress.out" 2>"$fixture_dir/progress.err"
grep -q 'command still running after 1s' "$fixture_dir/progress.err"

project_subtree="$compiled_root/${fixture_repo#/}"
mkdir -p "$project_subtree"
touch "$project_subtree/reused"
"${cache_command[@]}" path >/dev/null
[[ -f "$project_subtree/reused" ]]

printf '#lang racket/base\n;; Adds a temporary source path for structural invalidation coverage.\n' > "$probe_source"
"${cache_command[@]}" path >/dev/null
[[ ! -e "$project_subtree/reused" ]]

mkdir -p "$project_subtree"
touch "$project_subtree/removed"
rm -f -- "$probe_source"
"${cache_command[@]}" path >/dev/null
[[ ! -e "$project_subtree/removed" ]]

tracked_probe="$fixture_repo/tracked-probe.rhm"
printf '#lang rhombus\n// Exercises deletion of an indexed source before it is staged.\n' > "$tracked_probe"
git -C "$fixture_repo" add -- "$tracked_probe"
"${cache_command[@]}" path >/dev/null
mkdir -p "$project_subtree"
touch "$project_subtree/removed-indexed"
rm -f -- "$tracked_probe"
"${cache_command[@]}" path >/dev/null
[[ ! -e "$project_subtree/removed-indexed" ]]

cat > "$dependency_source" <<'EOF'
#lang racket/base
;; Supplies a value whose edit exercises dependency-directed invalidation.
(provide value)
(define value 1)
EOF
cat > "$importer_source" <<EOF
#lang racket/base
;; Reexports the cache test dependency through ordinary module bytecode.
(require "$(basename "$dependency_source")")
(provide observed)
(define observed value)
EOF
cat > "$runner_source" <<EOF
#lang racket/base
;; Observes a transitive cache-test dependency through ordinary module bytecode.
(require "$(basename "$importer_source")")
(displayln observed)
EOF
"${cache_command[@]}" path >/dev/null
"${cache_command[@]}" run env PLTCOLLECTS="$fixture_repo": \
  "$real_raco" make "$dependency_source" "$importer_source" "$runner_source"
[[ "$("${cache_command[@]}" run "$real_racket" "$runner_source")" == 1 ]]

dependency_name="$(basename "$dependency_source")"
importer_name="$(basename "$importer_source")"
runner_name="$(basename "$runner_source")"
dependency_bytecode="$project_subtree/compiled/${dependency_name//./_}.zo"
importer_bytecode="$project_subtree/compiled/${importer_name//./_}.zo"
runner_bytecode="$project_subtree/compiled/${runner_name//./_}.zo"
[[ -f "$dependency_bytecode" && -f "$importer_bytecode" && -f "$runner_bytecode" ]]
cat > "$dependency_source" <<'EOF'
#lang racket/base
;; Supplies the edited value used to verify reverse-dependency invalidation.
(provide value)
(define value 2)
EOF
"${cache_command[@]}" path >/dev/null
[[ ! -e "$dependency_bytecode" && ! -e "$importer_bytecode" && ! -e "$runner_bytecode" ]]
"${cache_command[@]}" run env PLT_COMPILED_FILE_CHECK=exists PLTCOLLECTS="$fixture_repo": \
  "$real_raco" make "$runner_source"
[[ "$("${cache_command[@]}" run "$real_racket" "$runner_source")" == 2 ]]

rm -f -- "$dependency_source" "$importer_source" "$runner_source"
"${cache_command[@]}" path >/dev/null

"${cache_command[@]}" clean
[[ ! -d "$compiled_root" ]]
