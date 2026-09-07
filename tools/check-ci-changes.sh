#!/usr/bin/env bash
# Verifies CI execution policy, dependency classification, and tracked executable coverage.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
classifier="$repo_dir/tools/ci-changes.sh"

if ! grep -Fq "group: ci-\${{ github.workflow }}-\${{ github.event.pull_request.number || github.run_id }}" \
    "$repo_dir/.github/workflows/ci.yml"; then
  echo "non-PR CI runs must use unique concurrency groups" >&2
  exit 1
fi
if ! grep -Fq "cancel-in-progress: \${{ github.event_name == 'pull_request' }}" \
    "$repo_dir/.github/workflows/ci.yml"; then
  echo "only superseded PR CI runs may be canceled" >&2
  exit 1
fi

classification_for() {
  "$classifier" --paths "$@"
}

field_value() {
  local output="$1"
  local field="$2"
  sed -n "s/^${field}=//p" <<< "$output"
}

check_field() {
  local path="$1"
  local field="$2"
  local expected="$3"
  local output actual
  output="$(classification_for "$path")"
  actual="$(field_value "$output" "$field")"
  if [[ "$actual" != "$expected" ]]; then
    echo "$path: expected $field=$expected, got $actual" >&2
    return 1
  fi
}

check_matrix_entry() {
  local path="$1"
  local field="$2"
  local target="$3"
  local output matrix
  output="$(classification_for "$path")"
  matrix="$(field_value "$output" "$field")"
  if [[ "$matrix" != *"\"target\":\"$target\""* ]]; then
    echo "$path: $field does not contain $target" >&2
    echo "$matrix" >&2
    return 1
  fi
}

check_no_jobs() {
  local path="$1"
  local output
  output="$(classification_for "$path")"
  if [[ "$(field_value "$output" host)" != false \
      || "$(field_value "$output" circt)" != false \
      || "$(field_value "$output" simulation)" != false \
      || "$(field_value "$output" programs)" != false \
      || "$(field_value "$output" examples)" != false ]]; then
    echo "$path: expected no CI jobs" >&2
    echo "$output" >&2
    return 1
  fi
}

check_no_jobs README.md
check_field sims/arch-test/configure.py program_arch true
check_field sims/arch-test/configure.py program_native false
check_field sims/program-test/isa.mk program_matrix '{"include":[{"suite":"isa"}]}'
check_field sims/program-test/isa.mk simulation false
check_field sims/arch-test/configure.py simulation false
check_field riscv/riscv-isa-tests program_matrix '{"include":[{"suite":"isa"},{"suite":"benchmark"}]}'
for path in cores/rv5stage/core.rhdl chi/link.rhdl noc/rtl/router.rhdl devices/aclint.rhdl socs/simple-soc.rhdl sims/TestDriver.v rhodium/backend/circt.rhm; do
  check_field "$path" program_matrix '{"include":[{"suite":"isa"},{"suite":"benchmark"}]}'
  check_field "$path" program_arch true
done
check_field tools/write-riscv-udb-config.rhm program_arch true
check_field tools/install-riscv-toolchain.sh programs true
check_field .github/workflows/ci.yml programs true
check_no_jobs flow/README.md
check_no_jobs flow/DEVELOPING.md
check_no_jobs tests/backend/README.md
check_no_jobs sram/README.md
check_no_jobs vlsi/sim/README.md
check_no_jobs tools/emacs/rhodium-mode.el
check_matrix_entry vlsi/src/rhodium-top.rhdl host_matrix ci-host-hygiene-test

check_matrix_entry rhodium/core/ir.rhm host_matrix ci-host-foundation-test
check_matrix_entry rhodium/core/ir.rhm circt_matrix ci-circt-language-test
check_field rhodium/core/ir.rhm simulation true
check_matrix_entry rhodium/analysis/clocking.rhm host_matrix ci-host-foundation-test
check_matrix_entry rhodium/analysis/clocking.rhm host_matrix ci-host-hygiene-test
check_matrix_entry rhodium/event/analyze.rhm host_matrix ci-host-foundation-test
check_matrix_entry rhodium/event/instrument.rhm host_matrix ci-host-backend-test
check_matrix_entry rhodium/event/analyze.rhm host_matrix ci-host-hygiene-test
check_matrix_entry rhodium/event/analyze.rhm circt_matrix ci-circt-language-test
check_matrix_entry rheg/runtime/rheg.cc circt_matrix ci-circt-language-test
check_matrix_entry rheg/perfetto/rheg_perfetto.cc host_matrix ci-host-backend-test
check_matrix_entry rheg/tests/event-collector-test.cpp circt_matrix ci-circt-language-test
check_field rhodium/event/analyze.rhm simulation false
check_matrix_entry tests/analysis/clocking-test.rhm host_matrix ci-host-foundation-test
check_matrix_entry tests/analysis/clocking-test.rhm host_matrix ci-host-hygiene-test
check_matrix_entry flow/main.rhdl host_matrix ci-host-cores-test
check_matrix_entry flow/main.rhdl host_matrix ci-host-socs-test
check_matrix_entry flow/main.rhdl host_matrix ci-host-hygiene-test
check_matrix_entry flow/main.rhdl circt_matrix ci-circt-std-test
check_matrix_entry flow/queue.rhdl host_matrix ci-host-foundation-test
check_matrix_entry flow/queue.rhdl host_matrix ci-host-backend-test
check_matrix_entry flow/queue.rhdl host_matrix ci-host-protocols-test
check_matrix_entry flow/queue.rhdl host_matrix ci-host-cores-test
check_matrix_entry flow/queue.rhdl host_matrix ci-host-socs-test
check_matrix_entry flow/queue.rhdl host_matrix ci-host-hygiene-test
check_matrix_entry flow/queue.rhdl circt_matrix ci-circt-std-test
check_matrix_entry flow/queue.rhdl circt_matrix ci-circt-protocols-test
check_matrix_entry flow/queue.rhdl circt_matrix ci-circt-cores-test
check_matrix_entry flow/queue.rhdl example_matrix examples-std
check_field flow/queue.rhdl simulation true
check_matrix_entry rhodium/std/ready-valid.rhdl circt_matrix ci-circt-std-test
check_matrix_entry rhodium/std/ready-valid.rhdl host_matrix ci-host-cores-test
# Flow must have an explicit dependency rule, not the unknown-path all-jobs fallback.
if [[ "$(classification_for flow/queue.rhdl)" != "$(classification_for rhodium/std/ready-valid.rhdl)" ]]; then
  echo "flow changes must retain the shared standard-library dependency matrix" >&2
  exit 1
fi
check_matrix_entry support/annotations.rhm host_matrix ci-host-foundation-test
check_matrix_entry tests/frontend/conditional-fixture.rhdl host_matrix ci-host-foundation-test
check_matrix_entry tests/frontend/invalid/bad-width.rhdl host_matrix ci-host-foundation-test
check_matrix_entry noc/rtl/router.rhdl host_matrix ci-host-models-test
check_matrix_entry noc/rtl/router.rhdl host_matrix ci-host-socs-test
check_matrix_entry noc/rtl/router.rhdl circt_matrix ci-circt-protocols-test
check_matrix_entry hardfloat/rtl/recode.rhdl host_matrix ci-host-models-test
check_matrix_entry hardfloat/rtl/recode.rhdl circt_matrix ci-circt-cores-test
check_matrix_entry devicetree/main.rhm host_matrix ci-host-models-test
check_field devicetree/main.rhm circt false
check_field devicetree/main.rhm simulation false
check_field hardfloat/tests/verilator/representation_tb.sv simulation true
check_matrix_entry chi/link.rhdl host_matrix ci-host-protocols-test
check_matrix_entry chi/link.rhdl host_matrix ci-host-socs-test
check_matrix_entry chi/link.rhdl host_matrix ci-host-hygiene-test
check_matrix_entry chi/link.rhdl circt_matrix ci-circt-protocols-test
check_matrix_entry chi/dpi-memory.rhdl host_matrix ci-host-protocols-test
check_matrix_entry chi/dpi-memory.rhdl circt_matrix ci-circt-protocols-test
check_field chi/dpi-memory.rhdl simulation true
check_field chi/dpi/chi_dpi_memory_dpi.cc simulation true
check_matrix_entry sims/fesvr/direct-memory-htif.rhdl circt_matrix ci-circt-protocols-test
check_field sims/fesvr/direct-memory-htif.rhdl simulation true
check_matrix_entry cores/rv5stage/core.rhdl host_matrix ci-host-cores-test
check_matrix_entry cores/rv5stage/core.rhdl host_matrix ci-host-socs-test
check_matrix_entry cores/rv5stage/core.rhdl circt_matrix ci-circt-cores-test
check_matrix_entry cores/rv5stage/core.rhdl example_matrix examples-rv5stage
check_field cores/rv5stage/core.rhdl simulation true
check_matrix_entry examples/rtl/alu.rhdl example_matrix examples-rhodium
check_matrix_entry examples/rtl/alu.rhdl host_matrix ci-host-hygiene-test
check_matrix_entry examples/rtl/alu.rhdl circt_matrix ci-circt-language-test
check_matrix_entry examples/clocking/single-clock.rhm example_matrix examples-clocking
check_matrix_entry examples/std/flow-control.rhdl example_matrix examples-std
check_matrix_entry examples/noc/noc-router.rhdl example_matrix examples-noc
check_matrix_entry examples/noc/noc-router.rhdl circt_matrix ci-circt-protocols-test
check_matrix_entry examples/noc/wormhole-router-diagram.rhdl example_matrix examples-noc
check_matrix_entry examples/lop/adder-core.rhm example_matrix examples-lop
check_matrix_entry examples/rfpl/circuit-pair.rhdl example_matrix examples-rfpl
check_matrix_entry examples/rfpl/circuit-pair.rhdl circt_matrix rfpl-circt-test
check_matrix_entry examples/riscv/instruction-fields.rhdl example_matrix examples-riscv
check_matrix_entry examples/riscv/instruction-fields.rhdl circt_matrix ci-circt-cores-test
check_matrix_entry examples/chi/ram.rhdl example_matrix examples-chi
check_matrix_entry examples/chi/ram.rhdl circt_matrix ci-circt-protocols-test
check_matrix_entry examples/cores/rv5stage.rhdl example_matrix examples-cores
check_matrix_entry examples/cores/rv5stage.rhdl circt_matrix ci-circt-cores-test
check_matrix_entry examples/rv5stage/core-diagram.rhdl example_matrix examples-rv5stage
check_matrix_entry tools/write-rv5stage-core-diagram.rhm example_matrix examples-rv5stage
check_matrix_entry tools/write-riscv-udb-config.rhm host_matrix ci-host-models-test
check_matrix_entry tools/write-riscv-udb-config.rhm host_matrix ci-host-cores-test
check_matrix_entry tools/write-riscv-udb-config.rhm host_matrix ci-host-socs-test
check_matrix_entry tools/write-noc-router-diagram.rhm example_matrix examples-noc
check_field tools/run-racket-tests.sh host true
check_field tools/run-racket-tests.sh circt false
check_field tools/run-racket-tests.sh examples true
check_field tools/run-racket-tests.sh simulation true
check_matrix_entry tools/check-parameter-annotations.rkt host_matrix ci-host-hygiene-test
check_matrix_entry tools/parameter-annotation-scope.txt host_matrix ci-host-hygiene-test
check_matrix_entry .githooks/pre-commit host_matrix ci-host-hygiene-test
check_matrix_entry socs/check-boundaries.sh host_matrix ci-host-hygiene-test
check_field tests/backend/verilog/adder_tb.sv circt true
check_field sims/fesvr/direct_mem_htif.cc simulation true
check_field sims/TestDriver.v simulation true
check_field sram/map-memories.py simulation true
check_field sram/circt/MemorySitePass.cpp simulation true
check_field vlsi/sim/Makefile simulation true
check_field vlsi/designs/mini-soc/sky130/sram-map.yaml simulation true
check_field sims/simple-soc-harness.rhdl simulation true
check_field sims/mini-soc-harness.rhdl simulation true
check_field sims/tiled-soc-harness.rhdl simulation true
check_field sims/emit-soc-harness.rhm simulation true
check_field sims/tests/direct-memory-htif-test.rhm simulation true
check_field socs/tests/simple-soc-test.rhm simulation true
check_matrix_entry socs/tests/simple-soc-test.rhm host_matrix ci-host-socs-test
check_field socs/tests/mini-soc-test.rhm simulation true
check_matrix_entry socs/tests/mini-soc-test.rhm host_matrix ci-host-socs-test
check_field socs/simple-soc.rhdl host true
check_matrix_entry socs/simple-soc.rhdl host_matrix ci-host-socs-test
check_field socs/simple-soc.rhdl circt true
check_field socs/simple-soc.rhdl simulation true
check_field socs/mini-soc.rhdl host true
check_matrix_entry socs/mini-soc.rhdl host_matrix ci-host-socs-test
check_field socs/mini-soc.rhdl circt true
check_field socs/mini-soc.rhdl simulation true
check_field unrecognized/new-tool.py host true
check_field unrecognized/new-tool.py circt true
check_field unrecognized/new-tool.py simulation true
check_field unrecognized/new-tool.py examples true

all_output="$(classification_for .github/workflows/ci.yml)"
if [[ "$(field_value "$all_output" host)" != true \
    || "$(field_value "$all_output" circt)" != true \
    || "$(field_value "$all_output" simulation)" != true \
    || "$(field_value "$all_output" examples)" != true ]]; then
  echo "the CI workflow must select every job" >&2
  exit 1
fi

while IFS= read -r path; do
  case "$path" in
    tests/emacs/*|tools/emacs/*)
      continue
      ;;
    vlsi/*)
      case "$path" in
        vlsi/sim/*|vlsi/designs/mini-soc/sky130/*)
          ;;
        *)
          continue
          ;;
      esac
      ;;
  esac
  case "$path" in
    Makefile|*.mk|*.inc|*.py|*.rhm|*.rhdl|*.rkt|*.rktd|*.sh|*.sv|*.cc|*.cpp|*.h|*.S|*.ld|*.rfpl|*.yml|*.yaml)
      output="$(classification_for "$path")"
      if [[ "$(field_value "$output" host)" != true \
          && "$(field_value "$output" circt)" != true \
          && "$(field_value "$output" simulation)" != true \
          && "$(field_value "$output" programs)" != true \
          && "$(field_value "$output" examples)" != true ]]; then
        echo "$path: tracked executable source selects no CI job" >&2
        exit 1
      fi
      ;;
  esac
done < <(git -C "$repo_dir" ls-files)

all_jobs="$($classifier --all)"
if [[ "$(field_value "$all_jobs" host)" != true \
    || "$(field_value "$all_jobs" circt)" != true \
    || "$(field_value "$all_jobs" simulation)" != true \
    || "$(field_value "$all_jobs" examples)" != true \
    || "$(field_value "$all_jobs" programs)" != true ]]; then
  echo "--all did not select every CI job" >&2
  exit 1
fi

fallback_jobs="$($classifier 0000000000000000000000000000000000000000 HEAD)"
if [[ "$fallback_jobs" != "$all_jobs" ]]; then
  echo "an unavailable base revision did not select every CI job" >&2
  exit 1
fi
