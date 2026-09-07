#!/usr/bin/env bash
# Classifies changed repository paths into dependency-aware CI job matrices.
set -euo pipefail

host_foundation=false
host_backend=false
host_models=false
host_protocols=false
host_cores=false
host_socs=false
host_hygiene=false
circt_language=false
circt_std=false
circt_protocols=false
circt_cores=false
circt_rfpl=false
simulation=false
examples=false
example_rtl=false
example_clocking=false
example_std=false
example_noc=false
example_lop=false
example_rfpl=false
example_riscv=false
example_chi=false
example_cores=false
example_rv5stage=false

mark_all_host() {
  host_foundation=true
  host_backend=true
  host_models=true
  host_protocols=true
  host_cores=true
  host_socs=true
  host_hygiene=true
}

mark_all_circt() {
  circt_language=true
  circt_std=true
  circt_protocols=true
  circt_cores=true
  circt_rfpl=true
}

mark_example_rtl() { examples=true; example_rtl=true; }
mark_example_clocking() { examples=true; example_clocking=true; }
mark_example_std() { examples=true; example_std=true; }
mark_example_noc() { examples=true; example_noc=true; }
mark_example_lop() { examples=true; example_lop=true; }
mark_example_rfpl() { examples=true; example_rfpl=true; }
mark_example_riscv() { examples=true; example_riscv=true; }
mark_example_chi() { examples=true; example_chi=true; }
mark_example_cores() { examples=true; example_cores=true; }
mark_example_rv5stage() { examples=true; example_rv5stage=true; }

mark_all_examples() {
  mark_example_rtl
  mark_example_clocking
  mark_example_std
  mark_example_noc
  mark_example_lop
  mark_example_rfpl
  mark_example_riscv
  mark_example_chi
  mark_example_cores
  mark_example_rv5stage
}

mark_all() {
  mark_all_host
  mark_all_circt
  simulation=true
  mark_all_examples
}

append_matrix_entry() {
  local variable="$1"
  local entry="$2"
  local current="${!variable}"
  [[ -n "$current" ]] && current+=,
  printf -v "$variable" '%s%s' "$current" "$entry"
}

emit_jobs() {
  local host=false
  local circt=false
  local host_matrix=""
  local circt_matrix=""
  local example_matrix=""

  if [[ "$host_foundation" == true ]]; then
    host=true
    append_matrix_entry host_matrix '{"name":"foundation","target":"ci-host-foundation-test"}'
  fi
  if [[ "$host_backend" == true ]]; then
    host=true
    append_matrix_entry host_matrix '{"name":"backend","target":"ci-host-backend-test"}'
  fi
  if [[ "$host_models" == true ]]; then
    host=true
    append_matrix_entry host_matrix '{"name":"models","target":"ci-host-models-test"}'
  fi
  if [[ "$host_protocols" == true ]]; then
    host=true
    append_matrix_entry host_matrix '{"name":"protocols","target":"ci-host-protocols-test"}'
  fi
  if [[ "$host_cores" == true ]]; then
    host=true
    append_matrix_entry host_matrix '{"name":"cores","target":"ci-host-cores-test"}'
  fi
  if [[ "$host_socs" == true ]]; then
    host=true
    append_matrix_entry host_matrix '{"name":"SoCs","target":"ci-host-socs-test"}'
  fi
  if [[ "$host_hygiene" == true ]]; then
    host=true
    append_matrix_entry host_matrix '{"name":"hygiene","target":"ci-host-hygiene-test"}'
  fi

  if [[ "$circt_language" == true ]]; then
    circt=true
    append_matrix_entry circt_matrix '{"name":"language","target":"ci-circt-language-test"}'
  fi
  if [[ "$circt_std" == true ]]; then
    circt=true
    append_matrix_entry circt_matrix '{"name":"standard library","target":"ci-circt-std-test"}'
  fi
  if [[ "$circt_protocols" == true ]]; then
    circt=true
    append_matrix_entry circt_matrix '{"name":"protocols","target":"ci-circt-protocols-test"}'
  fi
  if [[ "$circt_cores" == true ]]; then
    circt=true
    append_matrix_entry circt_matrix '{"name":"cores","target":"ci-circt-cores-test"}'
  fi
  if [[ "$circt_rfpl" == true ]]; then
    circt=true
    append_matrix_entry circt_matrix '{"name":"RFPL","target":"rfpl-circt-test"}'
  fi

  [[ "$example_rtl" == true ]] && append_matrix_entry example_matrix '{"name":"Rhodium","target":"examples-rhodium"}'
  [[ "$example_clocking" == true ]] && append_matrix_entry example_matrix '{"name":"clocking analysis","target":"examples-clocking"}'
  [[ "$example_std" == true ]] && append_matrix_entry example_matrix '{"name":"standard library","target":"examples-std"}'
  [[ "$example_noc" == true ]] && append_matrix_entry example_matrix '{"name":"NoC","target":"examples-noc"}'
  [[ "$example_lop" == true ]] && append_matrix_entry example_matrix '{"name":"language-oriented programming","target":"examples-lop"}'
  [[ "$example_rfpl" == true ]] && append_matrix_entry example_matrix '{"name":"RFPL","target":"examples-rfpl"}'
  [[ "$example_riscv" == true ]] && append_matrix_entry example_matrix '{"name":"RISC-V","target":"examples-riscv"}'
  [[ "$example_chi" == true ]] && append_matrix_entry example_matrix '{"name":"CHI","target":"examples-chi"}'
  [[ "$example_cores" == true ]] && append_matrix_entry example_matrix '{"name":"processor cores","target":"examples-cores"}'
  [[ "$example_rv5stage" == true ]] && append_matrix_entry example_matrix '{"name":"RV5Stage","target":"examples-rv5stage"}'

  echo "host=$host"
  echo "host_matrix={\"include\":[$host_matrix]}"
  echo "circt=$circt"
  echo "circt_matrix={\"include\":[$circt_matrix]}"
  echo "simulation=$simulation"
  echo "examples=$examples"
  echo "example_matrix={\"include\":[$example_matrix]}"
}

classify_path() {
  local path="$1"
  case "$path" in
    tests/emacs/*|tools/emacs/*)
      ;;
    *.rhm|*.rhdl)
      # Every maintained Rhombus source participates in annotation hygiene.
      host_hygiene=true
      ;;
  esac
  case "$path" in
    *.md|LICENSE|LICENSE.*|AGENTS.md|.gitignore|.gitattributes)
      # Documentation and repository metadata cannot affect executable behavior.
      ;;
    sram/*|vlsi/sim/*|vlsi/designs/mini-soc/sky130/*)
      simulation=true
      ;;
    tests/emacs/*|tools/emacs/*|vlsi/*)
      # Optional integrations have no functional CI; Rhombus files still run
      # repository-wide source hygiene through the classification above.
      ;;
    .github/workflows/ci.yml|tools/ci-changes.sh|tools/check-ci-changes.sh)
      mark_all
      ;;
    Makefile)
      mark_all
      ;;
    tools/check-example-verilog.sh)
      host_hygiene=true
      mark_all_circt
      mark_all_examples
      ;;
    tools/run-racket-tests.sh)
      mark_all_host
      mark_all_examples
      simulation=true
      ;;
    tools/write-rv5stage-core-diagram.rhm)
      mark_example_rv5stage
      ;;
    tools/write-riscv-udb-config.rhm)
      host_models=true
      host_cores=true
      host_socs=true
      ;;
    tools/write-noc-router-diagram.rhm)
      mark_example_noc
      ;;
    .githooks/pre-commit|tools/check-parameter-annotations.rkt|tools/parameter-annotation-scope.txt|tools/check-boundaries.sh|rfpl/check-boundaries.sh|noc/check-boundaries.sh|riscv/check-boundaries.sh|chi/check-boundaries.sh|cores/check-boundaries.sh|socs/check-boundaries.sh)
      host_hygiene=true
      ;;
    rhodium/event/*)
      host_foundation=true
      ;;
    rhodium/core/*|rhodium/analysis/*|rhodium/frontend/*|rhodium/base/*|rhodium/language.rhm|rhodium/main.rkt)
      mark_all
      ;;
    rhodium/std/*)
      host_hygiene=true
      host_foundation=true
      host_backend=true
      host_protocols=true
      host_cores=true
      host_socs=true
      circt_language=true
      circt_std=true
      circt_protocols=true
      circt_cores=true
      simulation=true
      mark_example_rtl
      mark_example_clocking
      mark_example_std
      mark_example_noc
      mark_example_riscv
      mark_example_chi
      mark_example_cores
      mark_example_rv5stage
      ;;
    rhodium/backend/*)
      host_backend=true
      mark_all_circt
      simulation=true
      ;;
    tests/core/*|tests/analysis/*|tests/frontend/*)
      host_foundation=true
      ;;
    tests/backend/*)
      host_backend=true
      mark_all_circt
      ;;
    examples/rtl/*)
      mark_example_rtl
      circt_language=true
      ;;
    examples/clocking/*)
      mark_example_clocking
      ;;
    examples/formal/*)
      # Formal examples are optional and are exercised by examples-formal.
      host_foundation=true
      ;;
    examples/std/*)
      mark_example_std
      circt_std=true
      ;;
    examples/noc/*)
      mark_example_noc
      circt_protocols=true
      ;;
    examples/lop/*)
      mark_example_lop
      circt_language=true
      ;;
    examples/rfpl/*)
      mark_example_rfpl
      circt_rfpl=true
      ;;
    examples/riscv/*)
      mark_example_riscv
      circt_cores=true
      ;;
    examples/chi/*)
      mark_example_chi
      circt_protocols=true
      ;;
    examples/cores/*)
      mark_example_cores
      circt_cores=true
      ;;
    examples/rv5stage/*)
      mark_example_rv5stage
      ;;
    examples/*)
      # Fail closed for new example groups until they receive an explicit shard.
      mark_all
      ;;
    rfpl/*)
      host_protocols=true
      circt_rfpl=true
      mark_example_rfpl
      ;;
    noc/*)
      host_models=true
      host_socs=true
      circt_protocols=true
      mark_example_noc
      ;;
    riscv/*)
      host_models=true
      host_cores=true
      host_socs=true
      circt_cores=true
      mark_example_riscv
      mark_example_cores
      mark_example_rv5stage
      ;;
    hardfloat/*)
      host_models=true
      circt_cores=true
      simulation=true
      ;;
    devicetree/*)
      host_models=true
      ;;
    chi/dpi-memory.rhdl|chi/dpi/*|chi/tests/dpi-memory-*|chi/tests/chi_dpi_memory_*)
      host_protocols=true
      host_socs=true
      circt_protocols=true
      simulation=true
      mark_example_chi
      ;;
    chi/*)
      host_protocols=true
      host_socs=true
      circt_protocols=true
      mark_example_chi
      ;;
    cores/*)
      host_cores=true
      host_socs=true
      circt_cores=true
      simulation=true
      mark_example_cores
      mark_example_rv5stage
      ;;
    sims/*)
      simulation=true
      ;;
    socs/*)
      host_socs=true
      circt_cores=true
      simulation=true
      ;;
    support/annotations.rhm|support/tests/*)
      host_foundation=true
      ;;
    support/README.md)
      host_foundation=true
      ;;
    tools/install-circt.sh)
      mark_all_circt
      simulation=true
      ;;
    *)
      # Unknown paths are executable until explicitly proven documentation-only.
      mark_all
      ;;
  esac
}

if [[ "${1:-}" == --all ]]; then
  mark_all
  emit_jobs
  exit 0
elif [[ "${1:-}" == --paths ]]; then
  shift
  for path in "$@"; do
    classify_path "$path"
  done
elif [[ $# == 2 ]]; then
  base_revision="$1"
  head_revision="$2"
  if [[ -z "$base_revision" || "$base_revision" =~ ^0+$ ]] \
      || ! git cat-file -e "$base_revision^{commit}" 2>/dev/null \
      || ! git cat-file -e "$head_revision^{commit}" 2>/dev/null; then
    mark_all
    emit_jobs
    exit 0
  fi
  if ! changed_paths="$(git diff --name-only "$base_revision" "$head_revision")"; then
    mark_all
    emit_jobs
    exit 0
  fi
  while IFS= read -r path; do
    [[ -n "$path" ]] && classify_path "$path"
  done <<< "$changed_paths"
else
  echo "usage: $0 --all | --paths PATH... | BASE_REVISION HEAD_REVISION" >&2
  exit 2
fi

emit_jobs
