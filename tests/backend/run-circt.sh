#!/usr/bin/env bash
# Checks CIRCT lowering, Verilog goldens, simulations, and event snapshot handoff.
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
    --verify-only|--simulate-only|--golden-only|--full|--update-goldens)
      if [[ "$mode" != run ]]; then
        echo "select at most one CIRCT test mode" >&2
        exit 2
      fi
      mode="$1"
      shift
      ;;
    *)
      echo "usage: $0 [--group language|std|protocols|cores|socs|rfpl] [--verify-only|--simulate-only|--golden-only|--full|--update-goldens]" >&2
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
  ""|language|std|protocols|cores|socs|rfpl) ;;
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
  async-read-memory sync-memory-masked sync-ram
  clocked-dpi assertions hierarchy bundle interface-array
  queue-options rr-arbiter packet-rr-arbiter round-robin-matcher ctrl-queue-options
  state-flow
  tiled-distribution
  dont-care decode noc-route-computer noc-router noc-network noc-wormhole noc-router-family noc-escape-router
  nested-bundle aggregate-memory one-hot-aggregate priority-encoder
  rv32i-alu rv64i-alu-integrated load-store-rv32-word bit-manip bit-manip-rv32
  credited-flow credited-monitor credited-monitor-overgrant flit-formats
  fesvr-mmio fesvr-boot aclint bootrom boot-address plic uart16550 uart-dpi chi-foundation chi-full-flits chi-link chi-monitor chi-transaction chi-retryable-transaction chi-transaction-sn chi-coherent chi-ram chi-home chi-coherent-home chi-inclusive-home chi-snp-noc chi-sn-noc chi-family-noc chi-router-composition chi-transfer-fragmenter
  rv5stage-core rv5stage-zcb rv5stage-mop rv5stage-wfi rv5stage-multiply rv5stage-dcache
)

repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
test_tmp_dir="$(mktemp -d /tmp/rhodium-circt.XXXXXX)"
trap 'rm -rf "$test_tmp_dir"' EXIT

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

cd "$repo_dir"

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
  local spec fixture top example design_export reference_export

  for spec in "${fixture_specs[@]}"; do
    IFS='|' read -r fixture top example design_export reference_export <<< "$spec"
    if [[ "$fixture" == "$wanted" ]]; then
      case "$group:$example" in
        language:examples/rtl/*|language:examples/lop/*|language:examples/clocking/*|std:examples/std/*|protocols:examples/noc/*|protocols:examples/chi/*|cores:examples/cores/*|cores:examples/riscv/*|socs:socs/tests/*|rfpl:examples/rfpl/*)
          return 0
          ;;
        *)
          return 1
          ;;
      esac
    fi
  done

  case "$group:$wanted" in
    language:nested-bundle|language:bundle-update|language:aggregate-memory|language:one-hot-aggregate|language:priority-encoder|language:formal-differential|language:event-runtime|language:event-pipeline|language:event-elastic|language:event-queue|language:event-arbiter|language:event-demux|language:event-atomic-fork|language:event-broadcast|language:event-join)
      return 0
      ;;
    std:round-robin-matcher|std:credited-flow|std:credited-monitor|std:credited-monitor-overgrant)
      return 0
      ;;
    protocols:fesvr-mmio|protocols:fesvr-boot|protocols:aclint|protocols:bootrom|protocols:boot-address|protocols:plic|protocols:uart16550|protocols:uart-dpi|protocols:noc-wormhole|protocols:noc-router-family|protocols:noc-escape-router|protocols:chi-*)
      return 0
      ;;
    cores:rv32i-*|cores:rv64i-*|cores:load-store|cores:load-store-rv32-word|cores:bit-manip*|cores:iterative-multiplier|cores:iterative-divider|cores:riscv-counters-*|cores:riscv-cmo|cores:riscv-floating-point|cores:riscv-compressed|cores:scoreboard|cores:rv5stage-*)
      return 0
      ;;
    *)
      return 1
      ;;
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
  "$circt_opt" "${circt_args[@]}" "$mlir" -o /dev/null \
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
  local design_export="${3:-design}"
  local reference_export="${4:-verilog_reference}"

  example_fixture_selected "$fixture" "$reference_export" || return 0
  [[ "$simulation_only" == false ]] || return 0
  prepare_example "$fixture" "$example" "$reference_export"
}

run_fixture() {
  local fixture="$1"
  local top="$2"
  local example="$3"
  local design_export="${4:-design}"
  local reference_export="${5:-verilog_reference}"
  local verilog="$test_tmp_dir/$fixture.sv"
  local object_dir="$test_tmp_dir/${fixture}_obj"
  local build_log="$test_tmp_dir/$fixture.verilator.log"
  local dpi_source="$repo_dir/tests/backend/verilog/${fixture}_dpi.cpp"

  example_fixture_selected "$fixture" "$reference_export" || return 0
  prepare_example "$fixture" "$example" "$reference_export"
  [[ "$simulate_fixtures" == true ]] || return 0

  local verilator_args=(
    --binary --timing --assert --build-jobs 0 --top-module "$top"
    --Mdir "$object_dir"
    "$verilog" "tests/backend/verilog/${fixture}_tb.sv"
  )
  if [[ -f "$dpi_source" ]]; then
    verilator_args+=("$dpi_source")
  fi

  if ! verilator "${verilator_args[@]}" > "$build_log" 2>&1; then
    cat "$build_log" >&2
    return 1
  fi
  "$object_dir/V$top"
}

run_expected_assertion_failure() {
  local fixture="$1"
  local top="$2"
  local testbench="$3"
  local expected_label="$4"
  local verilog="$test_tmp_dir/$fixture.sv"
  local object_dir="$test_tmp_dir/${fixture}_${top}_failure_obj"
  local build_log="$test_tmp_dir/$fixture.$top.failure.verilator.log"
  local run_log="$test_tmp_dir/$fixture.$top.failure.run.log"

  fixture_selected "$fixture" || return 0
  [[ "$simulate_fixtures" == true ]] || return 0

  if ! verilator --binary --timing --assert --build-jobs 0 --top-module "$top" \
      --Mdir "$object_dir" \
      "$verilog" "$testbench" \
      > "$build_log" 2>&1; then
    cat "$build_log" >&2
    return 1
  fi
  if bash -c '"$1"; status=$?; :; exit "$status"' _ "$object_dir/V$top" \
      > "$run_log" 2>&1; then
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
  local device_dpi_source="$repo_dir/devices/dpi/${fixture//-/_}.cc"
  local test_dpi_source="$repo_dir/tests/backend/verilog/${fixture}_dpi.cpp"
  local -a dpi_sources=()

  direct_fixture_selected "$fixture" "$top" || return 0

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
  "$circt_opt" "${circt_args[@]}" "$mlir" -o /dev/null > "$verilog"

  if [[ -f "$device_dpi_source" ]]; then
    dpi_sources+=("$device_dpi_source")
  fi
  if [[ -f "$test_dpi_source" ]]; then
    dpi_sources+=("$test_dpi_source")
  fi
  if [[ "$fixture" == event-runtime || "$fixture" == event-pipeline || "$fixture" == event-elastic || "$fixture" == event-queue || "$fixture" == event-arbiter || "$fixture" == event-demux || "$fixture" == event-atomic-fork || "$fixture" == event-broadcast || "$fixture" == event-join ]]; then
    dpi_sources+=("$repo_dir/rhodium/event/runtime/rhodium_event.cc")
  fi

  if [[ "$simulate_fixtures" == true && -n "$top" ]]; then
    if [[ -f "$test_tmp_dir/${fixture}_manifest.h" ]]; then
      verilator_args+=(-CFLAGS "-I$test_tmp_dir -I$repo_dir/rhodium/event/runtime")
    fi
    if [[ "$fixture" == event-runtime ]]; then
      bash "$repo_dir/tests/backend/run-event-collector.sh"
    fi
    # Match the SoC harness setting for the complete core's packed-interface
    # scheduling loops. Keep assertions and runtime convergence checks enabled.
    if [[ "$fixture" == rv5stage-io-boot ]]; then
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
    if ! verilator --binary --timing --assert --build-jobs 0 --top-module "$top" \
        "${verilator_args[@]+"${verilator_args[@]}"}" \
        --Mdir "$object_dir" \
        "$verilog" "tests/backend/verilog/${fixture}_tb.sv" \
        "${dpi_sources[@]+"${dpi_sources[@]}"}" \
        > "$build_log" 2>&1; then
      cat "$build_log" >&2
      return 1
    fi
    if (( ${#run_args[@]} > 0 )); then
      "$object_dir/V$top" "${run_args[@]}"
    else
      "$object_dir/V$top"
    fi
  fi
}

fixture_specs=(
  'adder|adder_tb|examples/lop/adder-standard.rhdl|design|verilog_reference'
  'adder4|adder4_tb|examples/rtl/adder4.rhdl|design|verilog_reference'
  'generated-adder|generated_adder_tb|examples/rtl/generated-adder.rhdl|design|verilog_reference'
  'alu|alu_tb|examples/rtl/alu.rhdl|design|verilog_reference'
  'enum-state|enum_state_tb|examples/rtl/enum-state.rhdl|design|verilog_reference'
  'enum-state-lookup||examples/rtl/enum-state.rhdl|lookup_design|lookup_verilog_reference'
  'enum-opcode||examples/rtl/enum-state.rhdl|opcode_design|opcode_verilog_reference'
  'one-hot|one_hot_tb|examples/rtl/one-hot.rhdl|design|verilog_reference'
  'one-hot-enum||examples/rtl/one-hot-enum.rhdl|design|verilog_reference'
  'masks||examples/rtl/masks.rhdl|design|verilog_reference'
  'shifts|shifts_tb|examples/rtl/shifts.rhdl|design|verilog_reference'
  'width-ops|width_ops_tb|examples/rtl/width-ops.rhdl|design|verilog_reference'
  'vector|vector_tb|examples/rtl/vector.rhdl|design|verilog_reference'
  'vector-carry||examples/rtl/vector.rhdl|carry_design|carry_verilog_reference'
  'vector-map||examples/rtl/vector.rhdl|map_design|map_verilog_reference'
  'vector-update|vector_update_tb|examples/rtl/vector-update.rhdl|design|verilog_reference'
  'vector-register-update|vector_register_update_tb|examples/rtl/vector-update.rhdl|register_design|register_verilog_reference'
  'vec-shift-register|vec_shift_register_tb|examples/rtl/vec-shift-register.rhdl|design|verilog_reference'
  'vec-shift-register-param|vec_shift_register_param_tb|examples/rtl/vec-shift-register-param.rhdl|design|verilog_reference'
  'predicate-filter|predicate_filter_tb|examples/rtl/predicate-filter.rhdl|design|verilog_reference'
  'wire|wire_tb|examples/rtl/wire.rhdl|design|verilog_reference'
  'async-read-memory|async_read_memory_tb|examples/rtl/async-read-memory.rhdl|design|verilog_reference'
  'sync-memory|sync_memory_tb|examples/rtl/sync-memory.rhdl|design|verilog_reference'
  'sync-memory-1rw|sync_memory_1rw_tb|examples/rtl/sync-memory-1rw.rhdl|design|verilog_reference'
  'sync-memory-masked|sync_memory_masked_tb|examples/rtl/sync-memory-masked.rhdl|design|verilog_reference'
  'multi-write-memory|multi_write_memory_tb|examples/rtl/multi-write-memory.rhdl|design|verilog_reference'
  'clocked-dpi|clocked_dpi_tb|examples/rtl/clocked-dpi.rhdl|design|verilog_reference'
  'clocked-dpi-always||examples/rtl/clocked-dpi.rhdl|always_design|always_verilog_reference'
  'clocked-dpi-explicit||examples/rtl/clocked-dpi.rhdl|explicit_design|explicit_verilog_reference'
  'assertions|assertions_tb|examples/rtl/assertions.rhdl|design|verilog_reference'
  'tiny-simd|tiny_simd_tb|examples/rtl/tiny-simd.rhdl|design|verilog_reference'
  'tiny-simd-no-multiply||examples/rtl/tiny-simd.rhdl|no_multiply_design|no_multiply_verilog_reference'
  'stack|stack_tb|examples/rtl/stack.rhdl|design|verilog_reference'
  'counter|counter_tb|examples/rtl/counter.rhdl|design|verilog_reference'
  'standard-counter|standard_counter_tb|examples/std/standard-counter.rhdl|design|verilog_reference'
  'multiply|multiply_tb|examples/rtl/multiply.rhdl|design|verilog_reference'
  'expanding-arithmetic|expanding_arithmetic_tb|examples/rtl/expanding-arithmetic.rhdl|design|verilog_reference'
  'fir-filter|fir_filter_tb|examples/rtl/fir-filter.rhdl|design|verilog_reference'
  'unsigned-comparisons|unsigned_comparisons_tb|examples/rtl/unsigned-comparisons.rhdl|design|verilog_reference'
  'signed-integers|signed_integers_tb|examples/rtl/signed-integers.rhdl|design|verilog_reference'
  'sync-counter||examples/rtl/sync-counter.rhdl|design|verilog_reference'
  'sync-counter-resetless||examples/rtl/sync-counter.rhdl|resetless_design|resetless_verilog_reference'
  'sync-counter-explicit-clock||examples/rtl/sync-counter.rhdl|explicit_clock_design|explicit_clock_verilog_reference'
  'enable-shift-register|enable_shift_register_tb|examples/rtl/enable-shift-register.rhdl|design|verilog_reference'
  'reset-shift-register|reset_shift_register_tb|examples/rtl/reset-shift-register.rhdl|design|verilog_reference'
  'clocking-environment||examples/clocking/frontend-environment.rhdl|design|verilog_reference'
  'clocking-missing-crossings-broken||examples/clocking/missing-crossings.rhdl|broken_design|broken_verilog_reference'
  'clocking-missing-crossings-fixed||examples/clocking/missing-crossings.rhdl|design|verilog_reference'
  'clocking-reconvergence||examples/clocking/reconvergence.rhdl|design|verilog_reference'
  'clocking-sync-level|clocking_sync_level_tb|examples/clocking/sync-level.rhdl|design|verilog_reference'
  'hierarchy|hierarchy_tb|examples/rtl/hierarchy.rhdl|design|verilog_reference'
  'rfpl-circuit-pair||examples/rfpl/circuit-pair.rhdl|design|verilog_reference'
  'nested-circuit|nested_circuit_tb|examples/rtl/nested-circuit.rhdl|design|verilog_reference'
  'bundle|bundle_tb|examples/rtl/bundle.rhdl|design|verilog_reference'
  'record-cast|record_cast_tb|examples/rtl/bundle.rhdl|cast_design|cast_verilog_reference'
  'bundle-specialization-types||examples/rtl/bundle.rhdl|specialization_design|specialization_verilog_reference'
  'bundle-conditional-specialization||examples/rtl/bundle.rhdl|conditional_specialization_design|conditional_specialization_verilog_reference'
  'bundle-nested-swap||examples/rtl/bundle.rhdl|nested_swap_design|nested_swap_verilog_reference'
  'bundle-hierarchy||examples/rtl/bundle.rhdl|hierarchy_design|hierarchy_verilog_reference'
  'tagged-union||examples/rtl/tagged-union.rhdl|design|verilog_reference'
  'nested-tagged-union||examples/rtl/nested-tagged-union.rhdl|design|verilog_reference'
  'interface|interface_tb|examples/rtl/interface.rhdl|design|verilog_reference'
  'interface-hierarchy||examples/rtl/interface.rhdl|hierarchy_design|hierarchy_verilog_reference'
  'interface-specialization||examples/rtl/interface-specialization.rhdl|design|verilog_reference'
  'interface-specialization-reversed||examples/rtl/interface-specialization.rhdl|reversed_design|reversed_verilog_reference'
  'interface-specialization-nested||examples/rtl/interface-specialization.rhdl|nested_design|nested_verilog_reference'
  'interface-specialization-width-adapter||examples/rtl/interface-specialization.rhdl|width_adapter_design|width_adapter_verilog_reference'
  'ready-valid-compatibility||examples/std/ready-valid-compatibility.rhdl|design|verilog_reference'
  'interface-array|interface_array_tb|examples/rtl/interface-array.rhdl|design|verilog_reference'
  'interface-generic-handle||examples/rtl/interface-array.rhdl|generic_handle_design|generic_handle_verilog_reference'
  'interface-parallel-handle||examples/rtl/interface-array.rhdl|parallel_handle_design|parallel_handle_verilog_reference'
  'interface-parallel-sink||examples/rtl/interface-array.rhdl|parallel_sink_design|parallel_sink_verilog_reference'
  'interface-array-hierarchy||examples/rtl/interface-array.rhdl|hierarchy_design|hierarchy_verilog_reference'
  'interface-array-sequence||examples/rtl/interface-array.rhdl|sequence_design|sequence_verilog_reference'
  'nested-interface|nested_interface_tb|examples/rtl/nested-interface.rhdl|design|verilog_reference'
  'nested-interface-member||examples/rtl/nested-interface.rhdl|member_design|member_verilog_reference'
  'nested-interface-deep||examples/rtl/nested-interface.rhdl|deep_design|deep_verilog_reference'
  'nested-interface-hierarchy||examples/rtl/nested-interface.rhdl|hierarchy_design|hierarchy_verilog_reference'
  'interface-monitor||examples/rtl/interface-monitor.rhdl|design|verilog_reference'
  'interface-transform||examples/rtl/interface-transform.rhdl|design|verilog_reference'
  'interface-transform-boundary||examples/rtl/interface-transform.rhdl|boundary_design|boundary_verilog_reference'
  'interface-transform-terminal||examples/rtl/interface-transform.rhdl|detached_terminal_design|detached_terminal_verilog_reference'
  'pipe|pipe_tb|examples/std/flow-control.rhdl|pipe_design|pipe_verilog_reference'
  'queue|queue_tb|examples/std/flow-control.rhdl|queue_design|queue_verilog_reference'
  'queue-one|queue_one_tb|examples/std/flow-control.rhdl|queue_one_design|queue_one_verilog_reference'
  'queue-options|queue_options_tb|examples/std/flow-control.rhdl|queue_options_design|queue_options_verilog_reference'
  'arbiter|arbiter_tb|examples/std/flow-control.rhdl|arbiter_design|arbiter_verilog_reference'
  'flow-chain|flow_chain_tb|examples/std/flow-control.rhdl|chain_design|chain_verilog_reference'
  'rr-arbiter|rr_arbiter_tb|examples/std/flow-topology.rhdl|rr_arbiter_design|rr_arbiter_verilog_reference'
  'packet-rr-arbiter|packet_rr_arbiter_tb|examples/std/packet-arbitration.rhdl|design|verilog_reference'
  'demux|demux_tb|examples/std/flow-topology.rhdl|demux_design|demux_verilog_reference'
  'join|join_tb|examples/std/flow-topology.rhdl|join_design|join_verilog_reference'
  'selective-join|selective_join_tb|examples/std/selective-join.rhdl|design|verilog_reference'
  'broadcast|broadcast_tb|examples/std/flow-topology.rhdl|broadcast_design|broadcast_verilog_reference'
  'atomic-fork|atomic_fork_tb|examples/std/flow-topology.rhdl|atomic_fork_design|atomic_fork_verilog_reference'
  'selective-atomic-fork|selective_atomic_fork_tb|examples/std/selective-atomic-fork.rhdl|design|verilog_reference'
  'flow-map|flow_map_tb|examples/std/flow-topology.rhdl|flow_map_design|flow_map_verilog_reference'
  'flow-filter||examples/std/flow-topology.rhdl|filter_flow_design|filter_flow_verilog_reference'
  'flow-gate||examples/std/flow-topology.rhdl|gate_flow_design|gate_flow_verilog_reference'
  'flow-endpoint-first||examples/std/flow-topology.rhdl|endpoint_first_design|endpoint_first_verilog_reference'
  'flow-fan-in-project||examples/std/flow-topology.rhdl|fan_in_project_design|fan_in_project_verilog_reference'
  'flow-zip-route||examples/std/flow-topology.rhdl|zip_route_design|zip_route_verilog_reference'
  'ctrl-pipe|ctrl_pipe_tb|examples/std/ctrl-flow.rhdl|ctrl_pipe_design|ctrl_pipe_verilog_reference'
  'ctrl-queue|ctrl_queue_tb|examples/std/ctrl-flow.rhdl|ctrl_queue_design|ctrl_queue_verilog_reference'
  'ctrl-queue-options|ctrl_queue_options_tb|examples/std/ctrl-flow.rhdl|ctrl_queue_options_design|ctrl_queue_options_verilog_reference'
  'ctrl-arbiter|ctrl_arbiter_tb|examples/std/ctrl-flow.rhdl|ctrl_arbiter_design|ctrl_arbiter_verilog_reference'
  'ctrl-rr-arbiter|ctrl_rr_arbiter_tb|examples/std/ctrl-flow.rhdl|ctrl_rr_arbiter_design|ctrl_rr_arbiter_verilog_reference'
  'ctrl-demux|ctrl_demux_tb|examples/std/ctrl-flow.rhdl|ctrl_demux_design|ctrl_demux_verilog_reference'
  'ctrl-join|ctrl_join_tb|examples/std/ctrl-flow.rhdl|ctrl_join_design|ctrl_join_verilog_reference'
  'ctrl-broadcast|ctrl_broadcast_tb|examples/std/ctrl-flow.rhdl|ctrl_broadcast_design|ctrl_broadcast_verilog_reference'
  'ctrl-chain||examples/std/ctrl-flow.rhdl|ctrl_chain_design|ctrl_chain_verilog_reference'
  'valid-map-fork||examples/std/valid-flow.rhdl|design|verilog_reference'
  'valid-filter||examples/std/valid-flow.rhdl|accepted_design|accepted_verilog_reference'
  'valid-to-decoupled||examples/std/valid-flow.rhdl|decoupled_design|decoupled_verilog_reference'
  'completion-queue||examples/std/completion-queue.rhdl|design|verilog_reference'
  'completion-queue-one||examples/std/completion-queue.rhdl|single_design|single_verilog_reference'
  'credited-flow|credited_flow_tb|examples/std/credited-transport.rhdl|design|verilog_reference'
  'credited-flow-chained||examples/std/credited-transport.rhdl|chained_design|chained_verilog_reference'
  'credited-monitor||examples/std/credited-transport.rhdl|monitor_design|monitor_verilog_reference'
  'flit-formats|flit_formats_tb|examples/std/flit-formats.rhdl|design|verilog_reference'
  'state-flow|state_flow_tb|examples/std/state-flow.rhdl|design|-'
  'tiled-distribution|tiled_distribution_tb|socs/tests/tiled-distribution-fixture.rhdl|distribution_design|-'
  'scoreboard|scoreboard_tb|examples/std/scoreboard.rhdl|design|verilog_reference'
  'full-adder||examples/rtl/full-adder.rhdl|design|verilog_reference'
  'adder-core||examples/lop/adder-core.rhm|design|verilog_reference'
  'adder-kernel||examples/lop/adder-kernel.rhm|design|verilog_reference'
  'adder-composed||examples/lop/adder-composed.rhdl|design|verilog_reference'
  'counter-composed||examples/lop/counter-composed.rhdl|design|verilog_reference'
  'bundle-kernel||examples/lop/bundle-kernel.rhdl|design|verilog_reference'
  'bundle-standard||examples/lop/bundle-standard.rhdl|design|verilog_reference'
  'interface-records||examples/lop/interface-records.rhdl|design|verilog_reference'
  'width-ops-kernel||examples/lop/width-ops-kernel.rhm|design|verilog_reference'
  'layered-adder||examples/rtl/layered-adder.rhdl|design|verilog_reference'
  'host-parameters||examples/rtl/host-parameters.rhdl|design|verilog_reference'
  'fresh-generators||examples/rtl/fresh-generators.rhdl|design|verilog_reference'
  'dont-care||examples/std/dont-care.rhdl|design|verilog_reference'
  'decode|decode_tb|examples/std/decode.rhdl|design|verilog_reference'
  'decode-composition||examples/std/decode-composition.rhdl|design|verilog_reference'
  'noc-crossbar|noc_crossbar_tb|examples/noc/noc-crossbar.rhdl|design|-'
  'noc-route-computer|noc_route_computer_tb|examples/noc/noc-route-computer.rhdl|design|verilog_reference'
  'noc-router|noc_router_tb|examples/noc/noc-router.rhdl|design|-'
  'noc-network|noc_network_tb|examples/noc/noc-network.rhdl|design|-'
  'generator-ordinary-defaults||examples/rtl/generator-parameters.rhdl|ordinary_defaults_design|ordinary_defaults_verilog_reference'
  'generator-ordinary-overrides||examples/rtl/generator-parameters.rhdl|ordinary_overrides_design|ordinary_overrides_verilog_reference'
  'generator-ordinary-typed-defaults||examples/rtl/generator-parameters.rhdl|ordinary_typed_defaults_design|ordinary_typed_defaults_verilog_reference'
  'generator-ordinary-required-keyword||examples/rtl/generator-parameters.rhdl|ordinary_required_keyword_design|ordinary_required_keyword_verilog_reference'
  'generator-sync-defaults||examples/rtl/generator-parameters.rhdl|sync_defaults_design|sync_defaults_verilog_reference'
  'generator-sync-overrides||examples/rtl/generator-parameters.rhdl|sync_overrides_design|sync_overrides_verilog_reference'
  'generator-sync-typed-defaults||examples/rtl/generator-parameters.rhdl|sync_typed_defaults_design|sync_typed_defaults_verilog_reference'
  'register-forms||examples/rtl/register-forms.rhdl|design|verilog_reference'
  'priority-encoder|priority_encoder_tb|examples/rtl/priority-encoder.rhdl|five_design|five_verilog_reference'
  'priority-encoder-shapes||examples/rtl/priority-encoder.rhdl|shapes_design|shapes_verilog_reference'
  'bit-negation||examples/rtl/bit-utilities.rhdl|negation_design|negation_verilog_reference'
  'bit-reductions||examples/rtl/bit-utilities.rhdl|reduction_design|reduction_verilog_reference'
  'bit-membership||examples/rtl/bit-utilities.rhdl|membership_design|membership_verilog_reference'
  'enum-validity||examples/rtl/bit-utilities.rhdl|enum_validity_design|enum_validity_verilog_reference'
  'sync-ram|sync_ram_tb|examples/std/sync-ram.rhdl|design|verilog_reference'
  'table|table_tb|examples/rtl/table.rhdl|design|verilog_reference'
  'valid-pipe|valid_pipe_tb|examples/std/valid-pipe.rhdl|design|verilog_reference'
  'valid-pipe-capture-always|valid_pipe_capture_always_tb|examples/std/valid-pipe.rhdl|capture_always_design|-'
  'vec-search|vec_search_tb|examples/rtl/vec-search.rhdl|design|verilog_reference'
  'riscv-instruction-fields||examples/riscv/instruction-fields.rhdl|design|verilog_reference'
  'rv64i-alu-integrated|rv64i_alu_integrated_tb|examples/cores/decoded-alu.rhdl|design|-'
  'chi-ram|chi_ram_tb|examples/chi/ram.rhdl|ram_design|-'
  'chi-home|chi_home_tb|examples/chi/home.rhdl|home_design|-'
  'rv5stage||examples/cores/rv5stage.rhdl|design|-'
)

direct_fixture_specs=(
  'event-runtime|event_runtime_tb'
  'event-pipeline|event_pipeline_tb'
  'event-elastic|event_elastic_tb'
  'event-queue|event_queue_tb'
  'event-arbiter|event_arbiter_tb'
  'event-demux|event_demux_tb'
  'event-atomic-fork|event_atomic_fork_tb'
  'event-broadcast|event_broadcast_tb'
  'event-join|event_join_tb'
  'aclint|aclint_tb'
  'bootrom|bootrom_tb'
  'fesvr-mmio|fesvr_mmio_tb'
  'fesvr-boot|fesvr_boot_tb'
  'boot-address|boot_address_tb'
  'plic|plic_tb'
  'uart16550|uart16550_tb'
  'uart-dpi|uart_dpi_tb'
  'nested-bundle|'
  'bundle-update|bundle_update_tb'
  'aggregate-memory|'
  'one-hot-aggregate|'
  'round-robin-matcher|round_robin_matcher_tb'
  'noc-wormhole|noc_wormhole_tb'
  'noc-escape-router|noc_escape_router_tb'
  'formal-differential|formal_differential_tb'
  'noc-router-family|noc_router_family_tb'
  'rv32i-alu|rv32i_alu_tb'
  'rv64i-alu|rv64i_alu_tb'
  'rv64i-alu-decode|'
  'credited-monitor-overgrant|'
  'chi-foundation|chi_foundation_tb'
  'chi-packets|chi_packets_tb'
  'chi-messages|chi_messages_tb'
  'chi-request-update|chi_request_update_tb'
  'chi-full-flits|'
  'chi-link|chi_link_tb'
  'chi-monitor|chi_monitor_tb'
  'chi-transaction|chi_transaction_tb'
  'chi-retryable-transaction|chi_retryable_transaction_tb'
  'chi-transaction-sn|chi_transaction_sn_tb'
  'chi-coherent|chi_coherent_tb'
  'chi-cache-maintenance|chi_cache_maintenance_tb'
  'chi-maintenance-home|chi_maintenance_home_tb'
  'chi-maintenance-inclusive|chi_maintenance_inclusive_tb'
  'chi-coherent-home|chi_coherent_home_tb'
  'chi-inclusive-home|chi_inclusive_home_tb'
  'chi-snp-noc|chi_snp_noc_tb'
  'chi-sn-noc|chi_sn_noc_tb'
  'chi-family-noc|chi_family_noc_tb'
  'chi-router-composition|'
  'chi-transfer-fragmenter|chi_transfer_fragmenter_tb'
  'load-store|load_store_tb'
  'riscv-cmo|riscv_cmo_tb'
  'load-store-rv32-word|load_store_rv32_word_tb'
  'bit-manip|bit_manip_tb'
  'bit-manip-rv32|bit_manip_rv32_tb'
  'iterative-multiplier|iterative_multiplier_tb'
  'iterative-divider|iterative_divider_tb'
  'riscv-counters-rv32|riscv_counters_rv32_tb'
  'riscv-floating-point|riscv_floating_point_tb'
  'riscv-compressed|riscv_compressed_tb'
  'rv5stage-fp-decoder|'
  'rv5stage-fp-register-file|rv5stage_fp_register_file_tb'
  'rv5stage-fp-pipeline|rv5stage_fp_pipeline_tb'
  'rv5stage-register-file|rv5stage_register_file_tb'
  'rv5stage-csr|rv5stage_csr_tb'
  'rv5stage-zihpm-rv32|rv5stage_zihpm_rv32_tb'
  'rv5stage-zihpm-rv64|rv5stage_zihpm_rv64_tb'
  'rv5stage-atomic|rv5stage_atomic_tb'
  'rv5stage-access-fault|rv5stage_access_fault_tb'
  'rv5stage-fetch|rv5stage_fetch_tb'
  'rv5stage-core|rv5stage_core_tb'
  'rv5stage-zcb|rv5stage_zcb_tb'
  'rv5stage-mop|rv5stage_mop_tb'
  'rv5stage-core-rv32f|rv5stage_core_rv32f_tb'
  'rv5stage-core-rv64d|rv5stage_core_rv64d_tb'
  'rv5stage-data-fault|rv5stage_data_fault_tb'
  'rv5stage-zicboz|rv5stage_zicboz_tb'
  'rv5stage-zicbom|rv5stage_zicbom_tb'
  'rv5stage-mmu-replay|rv5stage_mmu_replay_tb'
  'rv5stage-interrupt|rv5stage_interrupt_tb'
  'rv5stage-wfi|rv5stage_wfi_tb'
  'rv5stage-instruction-memory-router|rv5stage_instruction_memory_router_tb'
  'rv5stage-memory-router|rv5stage_memory_router_tb'
  'rv5stage-uncached|rv5stage_uncached_tb'
  'rv5stage-io-mshr|rv5stage_io_mshr_tb'
  'rv5stage-io-boot|rv5stage_io_boot_tb'
  'rv5stage-multiply|rv5stage_multiply_tb'
  'rv5stage-divide|rv5stage_divide_tb'
  'rv5stage-core-rv32|'
  'rv5stage-icache|rv5stage_icache_tb'
  'rv5stage-dcache|rv5stage_dcache_tb'
  'rv5stage-dcache-rv32|rv5stage_dcache_rv32_tb'
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

fixture_groups=(language std protocols cores socs rfpl)
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
  IFS='|' read -r fixture top example design_export reference_export <<< "$spec"
  if example_fixture_selected "$fixture" "$reference_export" \
      && [[ "$simulation_only" == false || -n "$top" ]]; then
    if [[ "$reference_export" != - \
        && ( "$compare_goldens" == true || "$update_goldens" == true ) ]]; then
      materialize_args+=(golden "$fixture" "$example" "$design_export" "$reference_export")
    else
      materialize_args+=(example "$fixture" "$example" "$design_export")
    fi
  fi
done

for spec in "${direct_fixture_specs[@]}"; do
  IFS='|' read -r fixture top <<< "$spec"
  if direct_fixture_selected "$fixture" "$top"; then
    materialize_args+=(emitter "$fixture" "tests/backend/emit-$fixture.rhm")
  fi
done

if (( ${#materialize_args[@]} > 0 )); then
  "$repo_dir/tools/run-racket.sh" -S "$repo_dir" tests/backend/load-example.rkt \
    materialize "$test_tmp_dir" "${materialize_args[@]}"
fi

for spec in "${fixture_specs[@]}"; do
  IFS='|' read -r fixture top example design_export reference_export <<< "$spec"
  if [[ -n "$top" ]]; then
    run_fixture "$fixture" "$top" "$example" "$design_export" "$reference_export"
  else
    lower_example_fixture "$fixture" "$example" "$design_export" "$reference_export"
  fi
done

for spec in "${direct_fixture_specs[@]}"; do
  IFS='|' read -r fixture top <<< "$spec"
  verify_fixture "$fixture" "$top"
done

run_expected_assertion_failure assertions assertions_fail_tb \
  tests/backend/verilog/assertions_fail_tb.sv request_holds
run_expected_assertion_failure credited-monitor \
  credited_monitor_underflow_tb \
  tests/backend/verilog/credited-monitor-underflow_tb.sv \
  credited_transfer_has_credit
run_expected_assertion_failure credited-monitor-overgrant \
  credited_monitor_overgrant_tb \
  tests/backend/verilog/credited-monitor-overgrant_tb.sv \
  credited_grant_within_limit
run_expected_assertion_failure chi-monitor \
  chi_monitor_unsupported_opcode_tb \
  tests/backend/verilog/chi-monitor-unsupported-opcode_tb.sv \
  chi_tx_req_opcode_supported
run_expected_assertion_failure chi-transaction \
  chi_transaction_duplicate_txn_tb \
  tests/backend/verilog/chi-transaction-duplicate-txn_tb.sv \
  chi_transaction_txn_id_unique
run_expected_assertion_failure chi-transaction \
  chi_transaction_early_data_tb \
  tests/backend/verilog/chi-transaction-early-data_tb.sv \
  chi_transaction_write_data_has_dbid
run_expected_assertion_failure chi-coherent \
  chi_coherent_early_comp_ack_tb \
  tests/backend/verilog/chi-coherent-early-comp-ack_tb.sv \
  chi_coherent_comp_ack_has_read_data
run_expected_assertion_failure chi-cache-maintenance \
  chi_cache_maintenance_wrong_source_tb \
  tests/backend/verilog/chi-cache-maintenance_tb.sv \
  chi_maintenance_response_source
run_expected_assertion_failure chi-cache-maintenance \
  chi_cache_maintenance_bad_address_tb \
  tests/backend/verilog/chi-cache-maintenance_tb.sv \
  chi_maintenance_aligned
run_expected_assertion_failure chi-ram chi_ram_invalid_tb \
  tests/backend/verilog/chi-ram-invalid_tb.sv \
  chi_ram_request_address_supported
for boot_address_case in hole alignment size source mask; do
  boot_address_assertion=boot_address_request_supported
  if [[ "$boot_address_case" == source || "$boot_address_case" == mask ]]; then
    boot_address_assertion=boot_address_write_data_supported
  fi
  run_expected_assertion_failure boot-address "boot_address_${boot_address_case}_tb" \
    tests/backend/verilog/boot-address-invalid-tb.sv "$boot_address_assertion"
done
run_expected_assertion_failure plic plic_invalid_access_tb \
  tests/backend/verilog/plic-invalid-access-tb.sv \
  plic_request_supported
run_expected_assertion_failure chi-home chi_home_wrong_response_source_tb \
  tests/backend/verilog/chi-home-wrong-source-tb.sv \
  chi_hni_transaction_response_transfer_paired
run_expected_assertion_failure chi-home chi_home_wrong_data_source_tb \
  tests/backend/verilog/chi-home-wrong-source-tb.sv \
  chi_hni_transaction_read_data_transfer_paired
