#!/usr/bin/env bash
# Enforces Rhodium source conventions, package imports, and standard/base profile composition.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

search_sources() {
  local pattern="$1"
  shift
  if command -v rg >/dev/null 2>&1; then
    rg -n "$pattern" "$@" --glob '*.rhm' --glob '*.rhdl' --glob '*.rkt'
  else
    find "$@" -type f \( -name '*.rhm' -o -name '*.rhdl' -o -name '*.rkt' \) \
      -exec grep -nHE "$pattern" {} +
  fi
}

fail_matches() {
  local description="$1"
  local pattern="$2"
  local directory="$3"
  local matches
  matches="$(search_sources "$pattern" "$directory" || true)"
  if [[ -n "$matches" ]]; then
    echo "$description" >&2
    echo "$matches" >&2
    exit 1
  fi
}

fail_matches "core must not import analysis, frontend, backend, or formal modules" \
  '^[[:space:]]+"[^"]*(analysis|frontend|backend|formal)/' rhodium/core
fail_matches "analysis must depend only on core and other analysis modules" \
  '^[[:space:]]+"[^"]*(frontend|backend|formal|std)/' rhodium/analysis
fail_matches "support annotations must remain dependency-neutral" \
  '^[[:space:]]+.*(rhodium/|noc/|riscv/|chi/|rfpl/|cores/)' support/annotations.rhm
fail_matches "frontend must not import backend or formal modules" \
  '^[[:space:]]+"[^"]*(backend|formal)/' rhodium/frontend
fail_matches "frontend must not import the optional standard library" \
  '^[[:space:]]+"[^"]*std/' rhodium/frontend
fail_matches "backend must not import frontend or formal modules" \
  '^[[:space:]]+"[^"]*(frontend|formal)/' rhodium/backend
fail_matches "formal engine must not import frontend, backend, or standard-library modules" \
  '(frontend/|backend/|std/)' rhodium/formal
fail_matches "diagram tooling must not import backend, formal, or standard-library modules" \
  '(backend/|formal/|std/)' rhodium/diagram
fail_matches "core must not import optional diagram tooling" \
  '^[[:space:]]+"[^"]*diagram/' rhodium/core
fail_matches "core must not import optional event tooling" \
  '^[[:space:]]+"[^"]*event/' rhodium/core
fail_matches "frontend must not import optional diagram tooling" \
  '^[[:space:]]+"[^"]*diagram/' rhodium/frontend
fail_matches "frontend must not import optional event tooling" \
  '^[[:space:]]+"[^"]*event/' rhodium/frontend
fail_matches "backends must not import optional diagram tooling" \
  '^[[:space:]]+"[^"]*diagram/' rhodium/backend
fail_matches "backends must not import optional event tooling" \
  '^[[:space:]]+"[^"]*event/' rhodium/backend
fail_matches "diagram tooling must not import optional event tooling" \
  '^[[:space:]]+"[^"]*event/' rhodium/diagram
fail_matches "event tooling must not import backend, formal, or standard-library modules" \
  '(backend/|formal/|std/)' rhodium/event
fail_matches "standard library must not import Rhodium implementation packages" \
  '^[[:space:]]+.*(core/|analysis/|backend/|event/|frontend/|formal/)' rhodium/std
fail_matches "Rhodium packages must not import the external CHI domain library" \
  '^[[:space:]]+.*chi/' rhodium
fail_matches "Rhodium packages must not import the external HardFloat domain library" \
  '^[[:space:]]+.*hardfloat/' rhodium
fail_matches "Rhodium packages must not import simulation harnesses" \
  '^[[:space:]]+.*sims/' rhodium
fail_matches "CHI must not import simulation harnesses" \
  '^[[:space:]]+.*sims/' chi
fail_matches "processor cores must not import simulation harnesses" \
  '^[[:space:]]+.*sims/' cores
fail_matches "devices must not import simulation harnesses" \
  '^[[:space:]]+.*sims/' devices
fail_matches "SoCs must expose hardware boundaries instead of importing simulation harnesses" \
  '^[[:space:]]+.*sims/' socs
for package in rhodium chi cores devices socs sims; do
  fail_matches "$package must not import post-CIRCT SRAM or VLSI policy" \
    '^[[:space:]]+.*(sram/|vlsi/)' "$package"
done
fail_matches "standard language assembly must not import analysis, core, backend, or formal modules" \
  '^[[:space:]]+"[^"]*(analysis|core|backend|formal)/' rhodium/language.rhm
fail_matches "base language assembly must not import analysis, core, backend, or formal modules" \
  '^[[:space:]]+"[^"]*(analysis|core|backend|formal)/' rhodium/base/language.rhm
fail_matches "the standard frontend must aggregate only the foundation and frontend layers" \
  '^[[:space:]]+"[^"]*(analysis/|core/|kernel\.rhm|support/)' rhodium/frontend/standard.rhm
fail_matches "the standard frontend must not implement feature behavior" \
  '^[[:space:]]*(def|fun|class|interface|operator|expr\.|defn\.|annot\.|dot\.|reducer\.)' rhodium/frontend/standard.rhm
fail_matches "the frontend foundation must not depend on layers or the standard aggregator" \
  '^[[:space:]]+"[^"]*(layers/|standard\.rhm)' rhodium/frontend/foundation.rhm
fail_matches "frontend support must not depend on profiles or language layers" \
  '^[[:space:]]+"[^"]*(foundation\.rhm|standard\.rhm|layers/)' rhodium/frontend/support
fail_matches "frontend layers must not depend on profiles or the standard aggregator" \
  '^[[:space:]]+"[^"]*(foundation\.rhm|standard\.rhm)' rhodium/frontend/layers
fail_matches "frontend layers must not import sibling layers" \
  '^[[:space:]]+"(bool|bundle|cast|comb|conditional|dpi|enum|expanding-arithmetic|hierarchy|interface|memory|one-hot|sequential|sync|sync-memory|vector|wire)\.rhm"' rhodium/frontend/layers

while IFS= read -r layer_file; do
  layer_name="$(basename "$layer_file")"
  if ! grep -Fq "| \`$layer_name\` |" rhodium/DEVELOPING.md; then
    echo "frontend layer is missing from the rhodium/DEVELOPING.md dependency table: $layer_file" >&2
    exit 1
  fi
done < <(find rhodium/frontend/layers -maxdepth 1 -type f -name '*.rhm' | sort)

while IFS= read -r std_file; do
  documented_path="${std_file#rhodium/}"
  dependency_row="$(grep -F "| \`$documented_path\` |" rhodium/DEVELOPING.md || true)"
  if [[ -z "$dependency_row" ]]; then
    echo "standard-library module is missing from the rhodium/DEVELOPING.md dependency table: $std_file" >&2
    exit 1
  fi
  if [[ "$documented_path" == "std/flow.rhdl" ]]; then
    continue
  fi
  while IFS= read -r dependency; do
    [[ -z "$dependency" ]] && continue
    documented_dependency="${dependency#rhodium/}"
    if [[ "$dependency_row" != *"\`$documented_dependency\`"* ]]; then
      echo "standard-library dependency is missing from the rhodium/DEVELOPING.md table: $documented_path imports $documented_dependency" >&2
      exit 1
    fi
  done < <(sed -n 's/.*lib("\(rhodium\/std\/[^\"]*\.rhdl\)").*/\1/p' "$std_file")
done < <(find rhodium/std -type f -name '*.rhdl' | sort)

fail_matches "core tests must not import analysis or backend modules" \
  '^[[:space:]]+"[^"]*(analysis|backend)/' tests/core
fail_matches "analysis tests must not import frontend, backend, formal, or standard-library modules" \
  '^[[:space:]]+"[^"]*(frontend|backend|formal|std)/' tests/analysis
fail_matches "frontend tests must not import backend modules" \
  '^[[:space:]]+"[^"]*backend/' tests/frontend

unexpected_top_level="$(find rhodium -maxdepth 1 -type f \
  ! -name 'README.md' ! -name 'DEVELOPING.md' ! -name 'CLOCKING_PLAN.md' \
  ! -name 'main.rkt' ! -name 'language.rhm' -print)"
if [[ -n "$unexpected_top_level" ]]; then
  echo "rhodium root may contain only architecture documents, the reader shim, and language assembly" >&2
  echo "$unexpected_top_level" >&2
  exit 1
fi

unexpected_racket="$(find rhodium -type f -name '*.rkt' \
  ! -path 'rhodium/main.rkt' \
  ! -path 'rhodium/base/main.rkt' \
  ! -path 'rhodium/base/lang/reader.rkt' \
  ! -path 'rhodium/formal/engine.rkt' -print)"
if [[ -n "$unexpected_racket" ]]; then
  echo "only #lang reader shims and the Rosette formal engine may use the .rkt extension" >&2
  echo "$unexpected_racket" >&2
  exit 1
fi

unexpected_rtl_sources="$(find . -path './.git' -prune -o -type f -name '*.rhdl' \
  ! -path './examples/*' ! -path './tests/frontend/*' ! -path './tests/formal/*' \
  ! -path './rhodium/std/*' ! -path './riscv/rtl/*' \
  ! -path './noc/rtl/*' \
  ! -path './devices/*' \
  ! -path './hardfloat/*' \
  ! -path './chi/*' \
  ! -path './sims/*' ! -path './socs/*' ! -path './cores/*' ! -path './vlsi/src/*' -print)"
if [[ -n "$unexpected_rtl_sources" ]]; then
  echo ".rhdl files may appear only in std, domain libraries, public adapters, examples, concrete cores and systems, simulation harnesses, physical-design fixtures, and frontend fixtures" >&2
  echo "$unexpected_rtl_sources" >&2
  exit 1
fi
