#!/usr/bin/env bash
# Exercises SoC import boundaries and failure propagation without ripgrep.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
fixture="$(mktemp -d /tmp/rhodium-soc-boundaries.XXXXXX)"
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/socs/tests" "$fixture/socs/mini-soc" "$fixture/bin" "$fixture/fail-bin"
cp "$repo_dir/socs/check-boundaries.sh" "$fixture/socs/check-boundaries.sh"
# Deliberately provide only the audit's portable dependencies, never rg.
for tool in bash dirname find grep; do
  ln -s "$(command -v "$tool")" "$fixture/bin/$tool"
done
printf '  "mini-soc/local.rhm"\n' > "$fixture/socs/mini-soc.rhdl"
printf '  "../mini-soc.rhdl"\n' > "$fixture/socs/tests/integration.rhm"
printf '  "shared.rhm"\n' > "$fixture/socs/shared.rhm"
audit() {
  PATH="$fixture/bin" bash "$fixture/socs/check-boundaries.sh"
}
expect_failure() {
  local expected=$1
  shift
  if "$@" > "$fixture/output" 2>&1; then
    echo "boundary audit unexpectedly succeeded" >&2
    exit 1
  fi
  grep -q "$expected" "$fixture/output"
}
audit
printf '  "simple-soc.rhdl"\n' > "$fixture/socs/shared.rhm"
expect_failure 'must not import simple-soc' audit
printf '  "../tiled-soc/main.rhdl"\n' > "$fixture/socs/mini-soc/local.rhm"
printf '  "shared.rhm"\n' > "$fixture/socs/shared.rhm"
expect_failure 'must not import tiled-soc' audit
printf '  "../shared.rhm"\n' > "$fixture/socs/mini-soc/local.rhm"
audit
for tool in find grep; do
  printf '#!/usr/bin/env bash\necho "injected %s failure" >&2\nexit 2\n' "$tool" > "$fixture/fail-bin/$tool"
  chmod +x "$fixture/fail-bin/$tool"
  expect_failure "injected $tool failure" env PATH="$fixture/fail-bin:$fixture/bin" \
    bash "$fixture/socs/check-boundaries.sh"
  rm "$fixture/fail-bin/$tool"
done
echo "SoC boundary audit regressions passed"
