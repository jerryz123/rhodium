#!/usr/bin/env bash
# Verifies persistent reuse, structural invalidation, command scoping, and cleanup.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fixture_dir="$(mktemp -d /tmp/rhodium-racket-build-cache-test.XXXXXX)"
fixture_repo="$fixture_dir/repository"
probe_source="$fixture_repo/probe.rkt"
dependency_source="$fixture_repo/dependency.rkt"
importer_source="$fixture_repo/importer.rkt"
runner_source="$fixture_repo/runner.rkt"
real_racket="$(command -v racket)"
real_raco="$(command -v raco)"

cleanup() {
  rm -rf -- "$fixture_dir"
}
trap cleanup EXIT

mkdir -p "$fixture_dir/packages" "$fixture_repo/tools"
cp "$repo_dir/tools/racket-build-cache.sh" "$fixture_repo/tools/racket-build-cache.sh"
cp "$repo_dir/tools/invalidate-racket-build-cache.rkt" \
  "$fixture_repo/tools/invalidate-racket-build-cache.rkt"
git -C "$fixture_repo" init -q
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
  RACKET="$fixture_dir/fake-racket"
  RACO="$fixture_dir/fake-raco"
  FAKE_PACKAGE_DIR="$fixture_dir/packages"
  REAL_RACKET="$real_racket"
  "$fixture_repo/tools/racket-build-cache.sh")

compiled_root="$("${cache_command[@]}" path)"
[[ -d "$compiled_root" ]]
[[ "$("${cache_command[@]}" path)" == "$compiled_root" ]]
[[ "$("${cache_command[@]}" run bash -c 'printf %s "$PLTCOMPILEDROOTS"')" == "$compiled_root" ]]

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
