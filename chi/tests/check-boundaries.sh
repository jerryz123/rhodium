#!/usr/bin/env bash
# Checks recursive CHI import auditing, pure NoC ownership, and tool failures.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
fixture="$(mktemp -d /tmp/rhodium-chi-boundaries.XXXXXX)"
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/chi/protocol/nested" "$fixture/chi/noc" "$fixture/chi/tests" \
  "$fixture/rhodium/std" "$fixture/bin" "$fixture/fail-bin"
cp "$repo_dir/chi/check-boundaries.sh" "$fixture/chi/check-boundaries.sh"
for tool in bash dirname find grep; do
  ln -s "$(command -v "$tool")" "$fixture/bin/$tool"
done
printf '#lang rhodium\n  lib("rhodium/std/bits.rhdl") open\n' > "$fixture/chi/protocol/nested/valid.rhdl"
printf '#lang rhombus\n  "../../noc/plan/main.rhm" open\n' > "$fixture/chi/noc/noc-authoring.rhm"
printf '  lib("rhodium/core/main.rhm") open\n' > "$fixture/chi/tests/fixture.rhm"
audit() {
  PATH="$fixture/bin" bash "$fixture/chi/check-boundaries.sh"
}
expect_failure() {
  local expected=$1
  shift
  if "$@" > "$fixture/output" 2>&1; then
    echo "CHI boundary audit unexpectedly succeeded" >&2
    exit 1
  fi
  grep -q "$expected" "$fixture/output"
}
audit
printf '  lib("rhodium/core/main.rhm") open\n' > "$fixture/chi/protocol/nested/valid.rhdl"
expect_failure 'CHI sources may import only public' audit
printf '  lib("rhodium/std/bits.rhdl") open\n' > "$fixture/chi/protocol/nested/valid.rhdl"
printf '  lib("rhodium/std/bits.rhdl") open\n' > "$fixture/chi/noc/noc-authoring.rhm"
expect_failure 'pure CHI-to-NoC compilation' audit
printf '  "../../noc/plan/main.rhm" open\n' > "$fixture/chi/noc/noc-authoring.rhm"
audit
for tool in find grep; do
  printf '#!/usr/bin/env bash\necho "injected %s failure" >&2\nexit 2\n' "$tool" > "$fixture/fail-bin/$tool"
  chmod +x "$fixture/fail-bin/$tool"
  expect_failure "injected $tool failure" env PATH="$fixture/fail-bin:$fixture/bin" \
    bash "$fixture/chi/check-boundaries.sh"
  rm "$fixture/fail-bin/$tool"
done
if command -v rg >/dev/null 2>&1; then
  ln -s "$(command -v rg)" "$fixture/bin/rg"
  audit
  printf '  lib("rhodium/backend/circt.rhm") open\n' > "$fixture/chi/protocol/nested/valid.rhdl"
  expect_failure 'CHI sources may import only public' audit
  printf '  lib("rhodium/std/bits.rhdl") open\n' > "$fixture/chi/protocol/nested/valid.rhdl"
  printf '#!/usr/bin/env bash\necho "injected rg failure" >&2\nexit 2\n' > "$fixture/fail-bin/rg"
  chmod +x "$fixture/fail-bin/rg"
  expect_failure 'injected rg failure' env PATH="$fixture/fail-bin:$fixture/bin" \
    bash "$fixture/chi/check-boundaries.sh"
fi
echo "CHI boundary audit regressions passed"
