#!/usr/bin/env bash
# Checks CIRCT lowering, Verilog goldens, simulations, and event snapshot handoff.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

mode=run
fixture_group="${FIXTURE_GROUP:-}"
while (( $# > 0 )); do
  case "$1" in
    --group)
      if (( $# < 2 )); then
        echo "--group requires a fixture group" >&2
        exit 2
      fi
      fixture_group="$2"
      shift 2
      ;;
    --list-fixtures|--list-example-sources|--verify-only|--simulate-only|--golden-only|--full|--update-goldens)
      if [[ "$mode" != run ]]; then
        echo "select at most one CIRCT test mode" >&2
        exit 2
      fi
      mode="$1"
      shift
      ;;
    *)
      echo "usage: $0 [--group language|std|protocols|cores|cores-components|cores-execution|cores-execution-datapath|socs|rfpl] [--list-fixtures|--list-example-sources|--verify-only|--simulate-only|--golden-only|--full|--update-goldens]" >&2
      exit 2
      ;;
  esac
done
fixture_scope=curated
compare_goldens=true
update_goldens=false
simulate_fixtures=true
simulation_only=false
run_direct_fixtures=true
case "$mode" in
  run) ;;
  --list-fixtures)
    compare_goldens=false
    simulate_fixtures=false
    ;;
  --list-example-sources)
    fixture_scope=all
    compare_goldens=false
    simulate_fixtures=false
    ;;
  --verify-only)
    compare_goldens=false
    simulate_fixtures=false
    ;;
  --simulate-only)
    compare_goldens=false
    simulation_only=true
    ;;
  --golden-only)
    fixture_scope=all
    simulate_fixtures=false
    run_direct_fixtures=false
    ;;
  --full)
    fixture_scope=all
    ;;
  --update-goldens)
    fixture_scope=all
    compare_goldens=false
    update_goldens=true
    simulate_fixtures=false
    run_direct_fixtures=false
    ;;
  *)
    echo "unsupported CIRCT test mode: $mode" >&2
    exit 2
    ;;
esac

if [[ -n "${FIXTURE:-}" && -n "${FIXTURES:-}" ]]; then
  echo "set either FIXTURE or FIXTURES, not both" >&2
  exit 2
fi
if [[ -n "$fixture_group" && ( -n "${FIXTURE:-}" || -n "${FIXTURES:-}" ) ]]; then
  echo "set either a fixture group or explicit fixtures, not both" >&2
  exit 2
fi
case "$fixture_group" in
  ""|language|std|protocols|cores|cores-components|cores-execution|cores-execution-datapath|socs|rfpl) ;;
  *)
    echo "unknown CIRCT fixture group: $fixture_group" >&2
    exit 2
    ;;
esac

# This semantic spine crosses every lowering family whose external-tool behavior
# is not already established by the backend's host-side text tests. FIXTURE
# always selects an explicit fixture, including fixtures outside this set.
integration_fixtures=(
  alu enum-state shifts signed-integers generated-adder
  formal-differential
  vector-update vec-shift-register-param
  async-read-memory
  clocked-dpi assertions hierarchy bundle interface-array
  tiled-time tiled-distribution
  dont-care
  nested-bundle aggregate-memory one-hot-aggregate priority-encoder
  uart-dpi chi-full-flits chi-router-composition
  )

repo_dir="$(cd "$(dirname "$0")/../../.." && pwd)"
test_tmp_dir="$(mktemp -d /tmp/rhodium-circt.XXXXXX)"
trap 'rm -rf "$test_tmp_dir"' EXIT

if [[ "$mode" != --list-fixtures && "$mode" != --list-example-sources ]]; then
  circt_opt="${CIRCT_OPT:-$repo_dir/.tools/firtool-1.155.0/bin/circt-opt}"
  if [[ ! -x "$circt_opt" ]]; then
    if command -v circt-opt >/dev/null 2>&1; then
      circt_opt="$(command -v circt-opt)"
    else
      echo "circt-opt not found; run 'make setup-circt' or set CIRCT_OPT" >&2
      exit 1
    fi
  fi

  golden_circt_version="firtool-1.155.0"
  circt_version="$("$circt_opt" --version | sed -n 's/^CIRCT //p')"
  if [[ "$compare_goldens" == true && "$circt_version" != "$golden_circt_version" ]]; then
    if [[ "$mode" == --golden-only ]]; then
      echo "Verilog goldens require CIRCT $golden_circt_version; found ${circt_version:-an unknown version}" >&2
      exit 1
    fi
    compare_goldens=false
    echo "CIRCT ${circt_version:-version unknown}: skipping version-specific Verilog golden comparisons"
  fi
fi

cd "$repo_dir"

owned_fixture_file() {
  local relative_path="$1"
  local required="${2:-true}"
  local -a matches=()

  while IFS= read -r match; do
    matches+=("$match")
  done < <(find . -type f -path "./*/tests/circt/$relative_path" -print | sort)
  if (( ${#matches[@]} == 1 )); then
    printf '%s\n' "${matches[0]#./}"
    return 0
  fi
  if (( ${#matches[@]} == 0 )) && [[ "$required" == false ]]; then
    return 1
  fi
  if (( ${#matches[@]} == 0 )); then
    echo "no package owns CIRCT fixture file: $relative_path" >&2
  else
    echo "multiple packages own CIRCT fixture file: $relative_path" >&2
    printf '  %s\n' "${matches[@]}" >&2
  fi
  exit 1
}

fixture_selected() {
  local fixture="$1"

  if [[ -n "${FIXTURE:-}" ]]; then
    [[ "$fixture" == "$FIXTURE" ]]
    return
  fi
  if [[ -n "${FIXTURES:-}" ]]; then
    local requested_fixture
    for requested_fixture in $FIXTURES; do
      [[ "$fixture" == "$requested_fixture" ]] && return 0
    done
    return 1
  fi
  if [[ -n "$fixture_group" ]]; then
    fixture_in_group "$fixture" "$fixture_group"
    return
  fi
  if [[ "$fixture_scope" == all ]]; then
    return 0
  fi
  local integration_fixture
  for integration_fixture in "${integration_fixtures[@]}"; do
    [[ "$fixture" == "$integration_fixture" ]] && return 0
  done
  return 1
}

example_fixture_selected() {
  local fixture="$1"
  local reference_export="$2"

  fixture_selected "$fixture" || return 1
  if [[ "$mode" == --golden-only || "$mode" == --update-goldens ]]; then
    [[ "$reference_export" != - ]]
    return
  fi
  return 0
}

fixture_in_group() {
  local wanted="$1"
  local group="$2"
  local spec fixture top example program_export reference_export

  if [[ "$group" == cores ]]; then
    for core_group in cores-components cores-execution; do
      fixture_in_group "$wanted" "$core_group" && return 0
    done
    return 1
  fi

  if [[ "$group" == cores-execution ]]; then
    for execution_group in cores-execution-datapath; do
      fixture_in_group "$wanted" "$execution_group" && return 0
    done
    return 1
  fi



  for spec in "${fixture_specs[@]}"; do
    IFS='|' read -r fixture top example program_export reference_export <<< "$spec"
    if [[ "$fixture" == "$wanted" ]]; then
      case "$group:$example" in
        language:examples/rtl/*|language:examples/lop/*|language:examples/clocking/*|std:examples/std/*|protocols:examples/noc/*|protocols:examples/chi/*|cores-components:examples/riscv/*|cores-components:examples/cores/decoded-alu.rhdl|cores-execution-datapath:examples/cores/rv5stage.rhdl|socs:socs/tests/*|rfpl:examples/rfpl/*)
          return 0
          ;;
        *)
          return 1
          ;;
      esac
    fi
  done

  case "$group:$wanted" in
    cores-execution-datapath:rv5stage-cosim*|cores-components:cosim-hooks|language:nested-bundle|language:aggregate-memory|language:one-hot-aggregate|language:formal-differential|language:event-runtime|language:event-elastic|protocols:uart-dpi|protocols:chi-full-flits|protocols:chi-router-composition)
      return 0
      ;;
    *) return 1 ;;
  esac
}

direct_fixture_selected() {
  local fixture="$1"
  local top="$2"

  fixture_selected "$fixture" || return 1
  [[ "$run_direct_fixtures" == true ]] || return 1
  if [[ "$simulation_only" == true && -z "$top" \
      && "$fixture" != credited-monitor \
      && "$fixture" != credited-monitor-overgrant ]]; then
    return 1
  fi
  return 0
}

time_phase() {
  local fixture="$1"
  local phase="$2"
  shift 2
  local TIMEFORMAT="[circt] $fixture $phase: %3Rs"
  time "$@"
}

build_verilator() {
  local fixture="$1"
  local build_log="$2"
  shift 2
  local TIMEFORMAT="[circt] $fixture build: %3Rs"
  if ! { time verilator "$@" > "$build_log" 2>&1; }; then
    cat "$build_log" >&2
    return 1
  fi
  grep '^- Verilator:' "$build_log" >&2 || true
}

update_reference() {
  local source_file="$1"
  local reference_export="$2"
  local verilog="$3"
  local rewritten="$test_tmp_dir/updated-$(basename "$source_file")"
  local marker="def $reference_export = @str|<<{"
  local marker_count

  marker_count="$(grep -Fxc "$marker" "$source_file" || true)"
  if [[ "$marker_count" != 1 ]]; then
    echo "$source_file must contain exactly one '$marker' line" >&2
    exit 1
  fi

  awk -v marker="$marker" -v replacement="$verilog" '
    $0 == marker {
      print
      while ((getline line < replacement) > 0) print line
      replacing = 1
      next
    }
    replacing && $0 == "}>>|" {
      print
      replacing = 0
      next
    }
    !replacing { print }
    END { if (replacing) exit 2 }
  ' "$source_file" > "$rewritten"
  mv "$rewritten" "$source_file"
}

prepare_example() {
  local fixture="$1"
  local example="$2"
  local reference_export="$3"
  local mlir="$test_tmp_dir/$fixture.mlir"
  local verilog="$test_tmp_dir/$fixture.sv"
  local expected="$test_tmp_dir/$fixture.expected.sv"
  local has_firmem=false
  local -a circt_args=(
    --strip-debuginfo-with-pred='drop-suffix=.mlir'
    --canonicalize
    --cse
    --prettify-verilog
  )

  if grep -q 'seq.hlmem' "$mlir"; then
    circt_args+=(--lower-seq-hlmem)
  fi
  if grep -q 'seq.firmem' "$mlir"; then
    circt_args+=(--lower-seq-firmem)
    has_firmem=true
  fi
  circt_args+=(
    --lower-sim-to-sv
    --lower-verif-to-sv
    --lower-seq-to-sv='disable-mem-randomization=true disable-reg-randomization=true'
  )
  if [[ "$has_firmem" == true ]]; then
    circt_args+=(--hw-memory-sim='disable-mem-randomization=true disable-reg-randomization=true read-enable-mode=undefined')
  fi
  circt_args+=(--sv-mask-non-synthesizable='mode=ifdef macro=SYNTHESIS')
  circt_args+=(--export-verilog)
  time_phase "$fixture" lowering "$circt_opt" "${circt_args[@]}" "$mlir" -o /dev/null \
    | sed -e '1{/^\/\/ Generated by CIRCT /d;}' \
          -e '/^\/\/ VCS coverage exclude_file$/d' \
    | perl -0pe 's/\n+\z//' > "$verilog"

  if [[ "$reference_export" == - ]]; then
    return 0
  fi

  if [[ "$update_goldens" == true ]]; then
    update_reference "$example" "$reference_export" "$verilog"
    return 0
  fi

  if [[ "$compare_goldens" != true ]]; then
    return 0
  fi

  if ! diff -u --label "$example:$reference_export" \
      --label "generated $fixture Verilog" "$expected" "$verilog"; then
    echo "$fixture Verilog differs from its example-owned reference" >&2
    exit 1
  fi
}

lower_example_fixture() {
  local fixture="$1"
  local example="$2"
  local reference_export="${3:-verilog_reference}"

  example_fixture_selected "$fixture" "$reference_export" || return 0
  # Assertions retains an HDL failure bench after its positive behavior moves to rsim.
  [[ "$simulation_only" == false || "$fixture" == assertions ]] || return 0
  prepare_example "$fixture" "$example" "$reference_export"
}

run_fixture() {
  local fixture="$1"
  local top="$2"
  local example="$3"
  local reference_export="${4:-verilog_reference}"
  local verilog="$test_tmp_dir/$fixture.sv"
  local object_dir="$test_tmp_dir/${fixture}_obj"
  local build_log="$test_tmp_dir/$fixture.verilator.log"
  local testbench
  local dpi_source

  example_fixture_selected "$fixture" "$reference_export" || return 0
  prepare_example "$fixture" "$example" "$reference_export"
  [[ "$simulate_fixtures" == true ]] || return 0
  testbench="$(owned_fixture_file "verilog/${fixture}_tb.sv")"
  dpi_source="$(owned_fixture_file "verilog/${fixture}_dpi.cpp" false || true)"

  local verilator_args=(
    --binary --timing --assert --build-jobs 0 --top-module "$top"
    --Mdir "$object_dir"
    "$verilog" "$testbench"
  )
  if [[ -f "$dpi_source" ]]; then
    verilator_args+=("$dpi_source")
  fi

  build_verilator "$fixture" "$build_log" "${verilator_args[@]}"
  time_phase "$fixture" simulation "$object_dir/V$top"
}

run_expected_assertion_failure() {
  local fixture="$1"
  local top="$2"
  local testbench="$3"
  local expected_label="$4"
  shift 4
  local verilog="$test_tmp_dir/$fixture.sv"
  local object_dir="$test_tmp_dir/${fixture}_${top}_failure_obj"
  local build_log="$test_tmp_dir/$fixture.$top.failure.verilator.log"
  local run_log="$test_tmp_dir/$fixture.$top.failure.run.log"
  local TIMEFORMAT="[circt] $fixture/$top simulation: %3Rs"

  fixture_selected "$fixture" || return 0
  [[ "$simulate_fixtures" == true ]] || return 0

  build_verilator "$fixture/$top" "$build_log" --binary --timing --assert --build-jobs 0 --top-module "$top" \
      --Mdir "$object_dir" \
      "$verilog" "$testbench" "$@"
  if { time bash -c '"$1"; status=$?; :; exit "$status"' _ "$object_dir/V$top" \
      > "$run_log" 2>&1; }; then
    echo "$fixture assertion failure simulation unexpectedly succeeded" >&2
    return 1
  fi
  if ! grep -q "$expected_label" "$run_log"; then
    echo "$fixture assertion failure did not report $expected_label" >&2
    cat "$run_log" >&2
    return 1
  fi
}

verify_fixture() {
  local fixture="$1"
  local top="${2:-}"
  local expected_label="${3:-}"
  local mlir="$test_tmp_dir/$fixture.mlir"
  local verilog="$test_tmp_dir/$fixture.sv"
  local has_firmem=false
  local -a circt_args=(
    --canonicalize
    --cse
    --prettify-verilog
  )
  local object_dir="$test_tmp_dir/${fixture}_obj"
  local build_log="$test_tmp_dir/$fixture.verilator.log"
  local -a run_args=()
  local -a verilator_args=()
  local device_dpi_source="$repo_dir/devices/uart/dpi/${fixture//-/_}.cc"
  local testbench
  local test_dpi_source
  local -a dpi_sources=()

  direct_fixture_selected "$fixture" "$top" || return 0
  test_dpi_source="$(owned_fixture_file "verilog/${fixture}_dpi.cpp" false || true)"

  if grep -q 'seq.hlmem' "$mlir"; then
    circt_args+=(--lower-seq-hlmem)
  fi
  if grep -q 'seq.firmem' "$mlir"; then
    circt_args+=(--lower-seq-firmem)
    has_firmem=true
  fi
  circt_args+=(
    --lower-sim-to-sv
    --lower-verif-to-sv
    --lower-seq-to-sv='disable-mem-randomization=true disable-reg-randomization=true'
  )
  if [[ "$has_firmem" == true ]]; then
    circt_args+=(--hw-memory-sim='disable-mem-randomization=true disable-reg-randomization=true read-enable-mode=undefined')
  fi
  circt_args+=(--sv-mask-non-synthesizable='mode=ifdef macro=SYNTHESIS')
  circt_args+=(--export-verilog)
  time_phase "$fixture" lowering "$circt_opt" "${circt_args[@]}" "$mlir" -o /dev/null > "$verilog"

  if [[ -n "$expected_label" ]]; then
    testbench="$(owned_fixture_file "verilog/${fixture}_tb.sv")"
    run_expected_assertion_failure "$fixture" "$top" "$testbench" "$expected_label"
    return 0
  fi

  if [[ -f "$device_dpi_source" ]]; then
    dpi_sources+=("$device_dpi_source")
  fi
  if [[ -f "$test_dpi_source" ]]; then
    dpi_sources+=("$test_dpi_source")
  fi
  if [[ "$fixture" == event-runtime || "$fixture" == event-elastic ]]; then
    dpi_sources+=("$repo_dir/rheg/runtime/rheg.cc")
  fi

  if [[ "$simulate_fixtures" == true && -n "$top" ]]; then
    if [[ "$fixture" == cosim-hooks || "$fixture" == rv5stage-cosim* ]]; then
      dpi_sources+=("$repo_dir/sims/cosim/events/collector.cc" "$repo_dir/sims/cosim/events/dpi.cc")
      verilator_args+=(-CFLAGS "-std=c++20")
      if [[ "$fixture" == rv5stage-cosim* ]]; then
        dpi_sources+=("$repo_dir/sims/cosim/rv5stage/adapter.cc" "$repo_dir/sims/cosim/rv5stage/vector.cc")
        verilator_args+=(--Wno-UNOPTFLAT "-I$repo_dir/cores/rv5stage/tests/circt/verilog")
      fi
    fi
    testbench="$(owned_fixture_file "verilog/${fixture}_tb.sv")"
    if [[ -f "$test_tmp_dir/${fixture}_manifest.h" ]]; then
      verilator_args+=(-CFLAGS "-I$test_tmp_dir -I$repo_dir/rheg/runtime")
    fi
    if [[ "$fixture" == event-runtime ]]; then
      bash "$repo_dir/rheg/tests/run-event-collector.sh"
    fi
    # Registered S2 replay feeds S0 through independent packed-interface leaves.
    # fetch-admission checks the actual leaf dependencies; match the SoC setting
    # without disabling assertions or runtime convergence checks.
    # Instrumented occurrences use top-derived names, so these fixtures
    # cannot be recognized by the original frontend module name.
    # The data router's packed bidirectional response carries ready back
    # through a selected IO-MSHR input; Verilator flags the whole struct as a
    # loop even though valid is independent of ready on every leaf.
    # Vector atomic issue couples ready with a calendar's payload-only latency
    # lookup. Packed structs look cyclic to Verilator; leaf-level RTL verification
    # remains enabled, as do simulation assertions and convergence checks.
    # RV2Wide's cache response similarly packs MEM data/hazards with independent
    # WB commit readiness; MEM/WB recovery only gates the younger EX lookup.
    # The core fixture's memory bus also packs WB admission with an independent
    # response offer and the completion arbiter's response readiness.
    if grep -Eq '^module RV5Stage(Frontend|VectorExecution)[ (_]' "$verilog"; then
      verilator_args+=(--Wno-UNOPTFLAT)
    fi
    if [[ "$fixture" == formal-differential && -n "${FORMAL_REPLAY_FILE:-}" ]]; then
      if [[ ! -f "$FORMAL_REPLAY_FILE" ]]; then
        echo "formal replay model file does not exist: $FORMAL_REPLAY_FILE" >&2
        return 1
      fi
      while IFS= read -r model_assignment; do
        if [[ ! "$model_assignment" =~ ^[A-Z_]+=[0-9]+$ ]]; then
          echo "invalid formal replay assignment: $model_assignment" >&2
          return 1
        fi
        run_args+=("+$model_assignment")
      done < "$FORMAL_REPLAY_FILE"
    fi
    build_verilator "$fixture" "$build_log" --binary --timing --assert --build-jobs 0 --top-module "$top" \
        "${verilator_args[@]+"${verilator_args[@]}"}" \
        --Mdir "$object_dir" \
        "$verilog" "$testbench" \
        "${dpi_sources[@]+"${dpi_sources[@]}"}"
    time_phase "$fixture" simulation "$object_dir/V$top" "${run_args[@]+"${run_args[@]}"}"
  fi
}

fixture_specs=(
  'adder||examples/lop/adder-standard.rhdl|program|verilog_reference'
  'adder4||examples/rtl/adder4.rhdl|program|verilog_reference'
  'generated-adder||examples/rtl/generated-adder.rhdl|program|verilog_reference'
  'alu||examples/rtl/alu.rhdl|program|verilog_reference'
  'enum-state||examples/rtl/enum-state.rhdl|program|verilog_reference'
  'enum-state-lookup||examples/rtl/enum-state.rhdl|lookup_program|lookup_verilog_reference'
  'enum-opcode||examples/rtl/enum-state.rhdl|opcode_program|opcode_verilog_reference'
  'one-hot||examples/rtl/one-hot.rhdl|program|verilog_reference'
  'one-hot-enum||examples/rtl/one-hot-enum.rhdl|program|verilog_reference'
  'masks||examples/rtl/masks.rhdl|program|verilog_reference'
  'shifts||examples/rtl/shifts.rhdl|program|verilog_reference'
  'width-ops||examples/rtl/width-ops.rhdl|program|verilog_reference'
  'vector||examples/rtl/vector.rhdl|program|verilog_reference'
  'vector-carry||examples/rtl/vector.rhdl|carry_program|carry_verilog_reference'
  'vector-map||examples/rtl/vector.rhdl|map_program|map_verilog_reference'
  'vector-update||examples/rtl/vector-update.rhdl|program|verilog_reference'
  'vector-register-update||examples/rtl/vector-update.rhdl|register_program|register_verilog_reference'
  'vec-shift-register||examples/rtl/vec-shift-register.rhdl|program|verilog_reference'
  'vec-shift-register-param||examples/rtl/vec-shift-register-param.rhdl|program|verilog_reference'
  'predicate-filter||examples/rtl/predicate-filter.rhdl|program|verilog_reference'
  'wire||examples/rtl/wire.rhdl|program|verilog_reference'
  'async-read-memory||examples/rtl/async-read-memory.rhdl|program|verilog_reference'
  'sync-memory||examples/rtl/sync-memory.rhdl|program|verilog_reference'
  'sync-memory-1rw||examples/rtl/sync-memory-1rw.rhdl|program|verilog_reference'
  'sync-memory-masked||examples/rtl/sync-memory-masked.rhdl|program|verilog_reference'
  'multi-write-memory||examples/rtl/multi-write-memory.rhdl|program|verilog_reference'
  'clocked-dpi|clocked_dpi_tb|examples/rtl/clocked-dpi.rhdl|program|verilog_reference'
  'clocked-dpi-always||examples/rtl/clocked-dpi.rhdl|always_program|always_verilog_reference'
  'clocked-dpi-explicit||examples/rtl/clocked-dpi.rhdl|explicit_program|explicit_verilog_reference'
  'assertions||examples/rtl/assertions.rhdl|program|verilog_reference'
  'tiny-simd||examples/rtl/tiny-simd.rhdl|program|verilog_reference'
  'tiny-simd-no-multiply||examples/rtl/tiny-simd.rhdl|no_multiply_program|no_multiply_verilog_reference'
  'stack||examples/rtl/stack.rhdl|program|verilog_reference'
  'counter||examples/rtl/counter.rhdl|program|verilog_reference'
  'standard-counter||examples/std/standard-counter.rhdl|program|verilog_reference'
  'multiply||examples/rtl/multiply.rhdl|program|verilog_reference'
  'expanding-arithmetic||examples/rtl/expanding-arithmetic.rhdl|program|verilog_reference'
  'fir-filter||examples/rtl/fir-filter.rhdl|program|verilog_reference'
  'unsigned-comparisons||examples/rtl/unsigned-comparisons.rhdl|program|verilog_reference'
  'signed-integers||examples/rtl/signed-integers.rhdl|program|verilog_reference'
  'sync-counter||examples/rtl/sync-counter.rhdl|program|verilog_reference'
  'sync-counter-resetless||examples/rtl/sync-counter.rhdl|resetless_program|resetless_verilog_reference'
  'sync-counter-explicit-clock||examples/rtl/sync-counter.rhdl|explicit_clock_program|explicit_clock_verilog_reference'
  'enable-shift-register||examples/rtl/enable-shift-register.rhdl|program|verilog_reference'
  'reset-shift-register||examples/rtl/reset-shift-register.rhdl|program|verilog_reference'
  'clocking-environment||examples/clocking/frontend-environment.rhdl|clocked_program|verilog_reference'
  'clocking-missing-crossings-broken||examples/clocking/missing-crossings.rhdl|broken_program|broken_verilog_reference'
  'clocking-missing-crossings-fixed||examples/clocking/missing-crossings.rhdl|clocked_program|verilog_reference'
  'clocking-reconvergence||examples/clocking/reconvergence.rhdl|clocked_program|verilog_reference'
  'clocking-sync-level||examples/clocking/sync-level.rhdl|clocked_program|verilog_reference'
  'hierarchy||examples/rtl/hierarchy.rhdl|program|verilog_reference'
  'rfpl-circuit-pair||examples/rfpl/circuit-pair.rhdl|program|verilog_reference'
  'nested-circuit||examples/rtl/nested-circuit.rhdl|program|verilog_reference'
  'bundle||examples/rtl/bundle.rhdl|program|verilog_reference'
  'record-cast||examples/rtl/bundle.rhdl|cast_program|cast_verilog_reference'
  'bundle-specialization-types||examples/rtl/bundle.rhdl|specialization_program|specialization_verilog_reference'
  'bundle-conditional-specialization||examples/rtl/bundle.rhdl|conditional_specialization_program|conditional_specialization_verilog_reference'
  'bundle-nested-swap||examples/rtl/bundle.rhdl|nested_swap_program|nested_swap_verilog_reference'
  'bundle-hierarchy||examples/rtl/bundle.rhdl|hierarchy_program|hierarchy_verilog_reference'
  'tagged-union||examples/rtl/tagged-union.rhdl|program|verilog_reference'
  'nested-tagged-union||examples/rtl/nested-tagged-union.rhdl|program|verilog_reference'
  'interface||examples/rtl/interface.rhdl|program|verilog_reference'
  'interface-hierarchy||examples/rtl/interface.rhdl|hierarchy_program|hierarchy_verilog_reference'
  'interface-specialization||examples/rtl/interface-specialization.rhdl|program|verilog_reference'
  'interface-specialization-reversed||examples/rtl/interface-specialization.rhdl|reversed_program|reversed_verilog_reference'
  'interface-specialization-nested||examples/rtl/interface-specialization.rhdl|nested_program|nested_verilog_reference'
  'interface-specialization-width-adapter||examples/rtl/interface-specialization.rhdl|width_adapter_program|width_adapter_verilog_reference'
  'ready-valid-compatibility||examples/std/ready-valid-compatibility.rhdl|program|verilog_reference'
  'interface-array||examples/rtl/interface-array.rhdl|program|verilog_reference'
  'interface-generic-handle||examples/rtl/interface-array.rhdl|generic_handle_program|generic_handle_verilog_reference'
  'interface-parallel-handle||examples/rtl/interface-array.rhdl|parallel_handle_program|parallel_handle_verilog_reference'
  'interface-parallel-sink||examples/rtl/interface-array.rhdl|parallel_sink_program|parallel_sink_verilog_reference'
  'interface-array-hierarchy||examples/rtl/interface-array.rhdl|hierarchy_program|hierarchy_verilog_reference'
  'interface-array-sequence||examples/rtl/interface-array.rhdl|sequence_program|sequence_verilog_reference'
  'nested-interface||examples/rtl/nested-interface.rhdl|program|verilog_reference'
  'nested-interface-member||examples/rtl/nested-interface.rhdl|member_program|member_verilog_reference'
  'nested-interface-deep||examples/rtl/nested-interface.rhdl|deep_program|deep_verilog_reference'
  'nested-interface-hierarchy||examples/rtl/nested-interface.rhdl|hierarchy_program|hierarchy_verilog_reference'
  'interface-monitor||examples/rtl/interface-monitor.rhdl|program|verilog_reference'
  'interface-transform||examples/rtl/interface-transform.rhdl|program|verilog_reference'
  'interface-transform-boundary||examples/rtl/interface-transform.rhdl|boundary_program|boundary_verilog_reference'
  'interface-transform-terminal||examples/rtl/interface-transform.rhdl|detached_terminal_program|detached_terminal_verilog_reference'
  'pipe||examples/std/flow-control.rhdl|pipe_program|pipe_verilog_reference'
  'queue||examples/std/flow-control.rhdl|queue_program|queue_verilog_reference'
  'queue-one||examples/std/flow-control.rhdl|queue_one_program|queue_one_verilog_reference'
  'queue-options||examples/std/flow-control.rhdl|queue_options_program|queue_options_verilog_reference'
  'arbiter||examples/std/flow-control.rhdl|arbiter_program|arbiter_verilog_reference'
  'flow-chain||examples/std/flow-control.rhdl|chain_program|chain_verilog_reference'
  'rr-arbiter||examples/std/flow-topology.rhdl|rr_arbiter_program|rr_arbiter_verilog_reference'
  'packet-rr-arbiter||examples/std/packet-arbitration.rhdl|program|verilog_reference'
  'demux||examples/std/flow-topology.rhdl|demux_program|demux_verilog_reference'
  'join||examples/std/flow-topology.rhdl|join_program|join_verilog_reference'
  'selective-join||examples/std/selective-join.rhdl|program|verilog_reference'
  'broadcast||examples/std/flow-topology.rhdl|broadcast_program|broadcast_verilog_reference'
  'atomic-fork||examples/std/flow-topology.rhdl|atomic_fork_program|atomic_fork_verilog_reference'
  'selective-atomic-fork||examples/std/selective-atomic-fork.rhdl|program|verilog_reference'
  'flow-map||examples/std/flow-topology.rhdl|flow_map_program|flow_map_verilog_reference'
  'flow-filter||examples/std/flow-topology.rhdl|filter_flow_program|filter_flow_verilog_reference'
  'flow-gate||examples/std/flow-topology.rhdl|gate_flow_program|gate_flow_verilog_reference'
  'flow-endpoint-first||examples/std/flow-topology.rhdl|endpoint_first_program|endpoint_first_verilog_reference'
  'flow-fan-in-project||examples/std/flow-topology.rhdl|fan_in_project_program|fan_in_project_verilog_reference'
  'flow-zip-route||examples/std/flow-topology.rhdl|zip_route_program|zip_route_verilog_reference'
  'ctrl-pipe||examples/std/ctrl-flow.rhdl|ctrl_pipe_program|ctrl_pipe_verilog_reference'
  'ctrl-queue||examples/std/ctrl-flow.rhdl|ctrl_queue_program|ctrl_queue_verilog_reference'
  'ctrl-queue-options||examples/std/ctrl-flow.rhdl|ctrl_queue_options_program|ctrl_queue_options_verilog_reference'
  'ctrl-arbiter||examples/std/ctrl-flow.rhdl|ctrl_arbiter_program|ctrl_arbiter_verilog_reference'
  'ctrl-rr-arbiter||examples/std/ctrl-flow.rhdl|ctrl_rr_arbiter_program|ctrl_rr_arbiter_verilog_reference'
  'ctrl-demux||examples/std/ctrl-flow.rhdl|ctrl_demux_program|ctrl_demux_verilog_reference'
  'ctrl-join||examples/std/ctrl-flow.rhdl|ctrl_join_program|ctrl_join_verilog_reference'
  'ctrl-broadcast||examples/std/ctrl-flow.rhdl|ctrl_broadcast_program|ctrl_broadcast_verilog_reference'
  'ctrl-chain||examples/std/ctrl-flow.rhdl|ctrl_chain_program|ctrl_chain_verilog_reference'
  'valid-map-fork||examples/std/valid-flow.rhdl|program|verilog_reference'
  'valid-filter||examples/std/valid-flow.rhdl|accepted_program|accepted_verilog_reference'
  'valid-to-decoupled||examples/std/valid-flow.rhdl|decoupled_program|decoupled_verilog_reference'
  'completion-queue||examples/std/completion-queue.rhdl|program|verilog_reference'
  'completion-queue-one||examples/std/completion-queue.rhdl|single_program|single_verilog_reference'
  'credited-flow||examples/std/credited-transport.rhdl|program|verilog_reference'
  'credited-flow-chained||examples/std/credited-transport.rhdl|chained_program|chained_verilog_reference'
  'credited-monitor||examples/std/credited-transport.rhdl|monitor_program|monitor_verilog_reference'
  'flit-formats||examples/std/flit-formats.rhdl|program|verilog_reference'
  'state-flow||examples/std/state-flow.rhdl|program|-'
  'tiled-distribution|tiled_distribution_tb|socs/tests/tiled-distribution-fixture.rhdl|distribution_design|-'
  'tiled-time|tiled_time_tb|socs/tests/tiled-distribution-fixture.rhdl|time_design|-'
  'scoreboard||examples/std/scoreboard.rhdl|program|verilog_reference'
  'full-adder||examples/rtl/full-adder.rhdl|program|verilog_reference'
  'adder-core||examples/lop/adder-core.rhm|program|verilog_reference'
  'adder-kernel||examples/lop/adder-kernel.rhm|program|verilog_reference'
  'adder-composed||examples/lop/adder-composed.rhdl|program|verilog_reference'
  'counter-composed||examples/lop/counter-composed.rhdl|program|verilog_reference'
  'bundle-kernel||examples/lop/bundle-kernel.rhdl|program|verilog_reference'
  'bundle-standard||examples/lop/bundle-standard.rhdl|program|verilog_reference'
  'interface-records||examples/lop/interface-records.rhdl|program|verilog_reference'
  'width-ops-kernel||examples/lop/width-ops-kernel.rhm|program|verilog_reference'
  'layered-adder||examples/rtl/layered-adder.rhdl|program|verilog_reference'
  'host-parameters||examples/rtl/host-parameters.rhdl|program|verilog_reference'
  'fresh-generators||examples/rtl/fresh-generators.rhdl|program|verilog_reference'
  'dont-care||examples/std/dont-care.rhdl|program|verilog_reference'
  'decode||examples/std/decode.rhdl|program|verilog_reference'
  'decode-composition||examples/std/decode-composition.rhdl|program|verilog_reference'
  'noc-crossbar||examples/noc/noc-crossbar.rhdl|program|-'
  'noc-route-computer||examples/noc/noc-route-computer.rhdl|program|verilog_reference'
  'noc-router||examples/noc/noc-router.rhdl|program|-'
  'noc-network||examples/noc/noc-network.rhdl|program|-'
  'generator-ordinary-defaults||examples/rtl/generator-parameters.rhdl|ordinary_defaults_program|ordinary_defaults_verilog_reference'
  'generator-ordinary-overrides||examples/rtl/generator-parameters.rhdl|ordinary_overrides_program|ordinary_overrides_verilog_reference'
  'generator-ordinary-typed-defaults||examples/rtl/generator-parameters.rhdl|ordinary_typed_defaults_program|ordinary_typed_defaults_verilog_reference'
  'generator-ordinary-required-keyword||examples/rtl/generator-parameters.rhdl|ordinary_required_keyword_program|ordinary_required_keyword_verilog_reference'
  'generator-sync-defaults||examples/rtl/generator-parameters.rhdl|sync_defaults_program|sync_defaults_verilog_reference'
  'generator-sync-overrides||examples/rtl/generator-parameters.rhdl|sync_overrides_program|sync_overrides_verilog_reference'
  'generator-sync-typed-defaults||examples/rtl/generator-parameters.rhdl|sync_typed_defaults_program|sync_typed_defaults_verilog_reference'
  'register-forms||examples/rtl/register-forms.rhdl|program|verilog_reference'
  'priority-encoder||examples/rtl/priority-encoder.rhdl|five_program|five_verilog_reference'
  'priority-encoder-shapes||examples/rtl/priority-encoder.rhdl|shapes_program|shapes_verilog_reference'
  'bit-negation||examples/rtl/bit-utilities.rhdl|negation_program|negation_verilog_reference'
  'bit-reductions||examples/rtl/bit-utilities.rhdl|reduction_program|reduction_verilog_reference'
  'bit-membership||examples/rtl/bit-utilities.rhdl|membership_program|membership_verilog_reference'
  'enum-validity||examples/rtl/bit-utilities.rhdl|enum_validity_program|enum_validity_verilog_reference'
  'sync-ram||examples/std/sync-ram.rhdl|program|verilog_reference'
  'table||examples/rtl/table.rhdl|program|verilog_reference'
  'valid-pipe||examples/std/valid-pipe.rhdl|program|verilog_reference'
  'valid-pipe-capture-always||examples/std/valid-pipe.rhdl|capture_always_program|-'
  'vec-search||examples/rtl/vec-search.rhdl|program|verilog_reference'
  'riscv-instruction-fields||examples/riscv/instruction-fields.rhdl|program|verilog_reference'
  'rv64i-alu-integrated||examples/cores/decoded-alu.rhdl|program|-'
  'chi-ram||examples/chi/ram.rhdl|ram_program|-'
  'chi-home||examples/chi/home.rhdl|home_program|-'
  'rv5stage||examples/cores/rv5stage.rhdl|program|-'
)

direct_fixture_specs=(
  'rv5stage-cosim|rv5stage_cosim_tb'
  'rv5stage-cosim32|rv5stage_cosim_tb'
  'rv5stage-cosim-vector|rv5stage_cosim_vector_tb'
  'event-runtime|event_runtime_tb'
  'cosim-hooks|cosim_hooks_tb||program'
  'event-elastic|event_elastic_tb'
  'uart-dpi|uart_dpi_tb||program'
  'nested-bundle|||program'
  'aggregate-memory|'
  'one-hot-aggregate|||program'
  'formal-differential|formal_differential_tb||program'
  'chi-full-flits|||program'
  'chi-router-composition|||program'
)

fixture_declared() {
  local wanted="$1"
  local spec fixture
  for spec in "${fixture_specs[@]}" "${direct_fixture_specs[@]}"; do
    IFS='|' read -r fixture _ <<< "$spec"
    [[ "$fixture" == "$wanted" ]] && return 0
  done
  return 1
}

fixture_has_golden() {
  local wanted="$1"
  local spec fixture reference_export
  for spec in "${fixture_specs[@]}"; do
    IFS='|' read -r fixture _ _ _ reference_export <<< "$spec"
    if [[ "$fixture" == "$wanted" ]]; then
      [[ "$reference_export" != - ]]
      return
    fi
  done
  return 1
}

for integration_fixture in "${integration_fixtures[@]}"; do
  if ! fixture_declared "$integration_fixture"; then
    echo "curated CIRCT fixture is not declared: $integration_fixture" >&2
    exit 1
  fi
done

for requested_fixture in ${FIXTURE:-} ${FIXTURES:-}; do
  if ! fixture_declared "$requested_fixture"; then
    echo "requested CIRCT fixture is not declared: $requested_fixture" >&2
    exit 1
  fi
  if [[ "$mode" == --golden-only || "$mode" == --update-goldens ]] \
      && ! fixture_has_golden "$requested_fixture"; then
    echo "requested CIRCT fixture has no Verilog golden: $requested_fixture" >&2
    exit 1
  fi
done

if [[ "$mode" == --list-example-sources ]]; then
  for spec in "${fixture_specs[@]}"; do
    IFS='|' read -r fixture _ example _ _ <<< "$spec"
    if fixture_selected "$fixture"; then
      printf '%s\n' "$example"
    fi
  done | sort -u
  exit 0
fi

for direct_spec in "${direct_fixture_specs[@]}"; do
  IFS='|' read -r direct_fixture _ <<< "$direct_spec"
  for example_spec in "${fixture_specs[@]}"; do
    IFS='|' read -r example_fixture _ <<< "$example_spec"
    if [[ "$direct_fixture" == "$example_fixture" ]]; then
      echo "CIRCT fixture has duplicate example and direct paths: $direct_fixture" >&2
      exit 1
    fi
  done
done

if [[ "$mode" == --list-fixtures ]]; then
  for spec in "${fixture_specs[@]}" "${direct_fixture_specs[@]}"; do
    IFS='|' read -r fixture _ <<< "$spec"
    fixture_selected "$fixture" && printf '%s\n' "$fixture"
  done
  exit 0
fi

fixture_groups=(language std protocols cores-components cores-execution-datapath socs rfpl)
for spec in "${fixture_specs[@]}" "${direct_fixture_specs[@]}"; do
  IFS='|' read -r fixture _ <<< "$spec"
  group_count=0
  for group in "${fixture_groups[@]}"; do
    if fixture_in_group "$fixture" "$group"; then
      (( group_count += 1 ))
    fi
  done
  if (( group_count != 1 )); then
    echo "CIRCT fixture must belong to exactly one group: $fixture" >&2
    exit 1
  fi
done

materialize_args=()
for spec in "${fixture_specs[@]}"; do
  IFS='|' read -r fixture top example program_export reference_export <<< "$spec"
  if example_fixture_selected "$fixture" "$reference_export" \
      && [[ "$simulation_only" == false || -n "$top" || "$fixture" == assertions ]]; then
    if [[ "$reference_export" != - \
        && ( "$compare_goldens" == true || "$update_goldens" == true ) ]]; then
      materialize_args+=(golden "$fixture" "$example" "$program_export" "$reference_export")
    else
      materialize_args+=(program "$fixture" "$example" "$program_export")
    fi
  fi
done

for spec in "${direct_fixture_specs[@]}"; do
  IFS='|' read -r fixture top expected_label program_export <<< "$spec"
  if direct_fixture_selected "$fixture" "$top"; then
    source="$(owned_fixture_file "emit-$fixture.rhm")"
    if [[ -n "$program_export" ]]; then
      materialize_args+=(program "$fixture" "$source" "$program_export")
    else
      materialize_args+=(emitter "$fixture" "$source")
    fi
  fi
done

if (( ${#materialize_args[@]} > 0 )); then
  time_phase batch materialization "$repo_dir/tools/run-racket.sh" -S "$repo_dir" tools/testing/circt/load-example.rhm \
    materialize "$test_tmp_dir" "${materialize_args[@]}"
fi

for spec in "${fixture_specs[@]}"; do
  IFS='|' read -r fixture top example program_export reference_export <<< "$spec"
  if [[ -n "$top" ]]; then
    run_fixture "$fixture" "$top" "$example" "$reference_export"
  else
    lower_example_fixture "$fixture" "$example" "$reference_export"
  fi
done

for spec in "${direct_fixture_specs[@]}"; do
  IFS='|' read -r fixture top expected_label program_export <<< "$spec"
  verify_fixture "$fixture" "$top" "$expected_label"
done

run_expected_assertion_failure assertions assertions_fail_tb \
  rhodium/backend/tests/circt/verilog/assertions_fail_tb.sv request_holds
