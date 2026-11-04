#!/usr/bin/env bash
# Checks CIRCT lowering, Verilog goldens, simulations, and event snapshot handoff.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$repo_dir"
export PATH="$repo_dir/.tools/verilator/bin:$PATH"
fixture_specs=()
direct_fixture_specs=()
group_members=()
while IFS='|' read -r group kind spec; do
  [[ -n "$group" && "${group:0:1}" != "#" ]] || continue
  case "$kind" in
    example) fixture_specs+=("$spec") ;;
    direct) direct_fixture_specs+=("$spec") ;;
    *) echo "invalid CIRCT fixture kind: $kind" >&2; exit 1 ;;
  esac
  IFS='|' read -r fixture top source program reference <<< "$spec"
  group_members+=("$group|$fixture")
  if [[ "${1:-}" == --list-inputs ]]; then
    # Report ownership patterns without requiring the paths to still exist.
    if [[ "$kind" == direct ]]; then source="*/tests/circt/emit-$fixture.rhm"; fi
    printf '%s\t%s\n' "$group" "$source"
    if [[ -n "$top" ]]; then
      printf '%s\t%s\n' "$group" "*/tests/circt/verilog/${fixture}_tb.sv" \
        "$group" "*/tests/circt/verilog/${fixture}_dpi.cpp"
    fi
  elif [[ "${1:-}" == --list-example-exports && "$kind" == example ]]; then
    printf '%s|%s|%s\n' "$source" "$program" "$reference"
  fi
done < tools/testing/circt/fixtures.txt
if [[ "${1:-}" == --list-inputs || "${1:-}" == --list-example-exports ]]; then
  [[ $# == 1 ]] || { echo "$1 takes no arguments" >&2; exit 2; }
  exit 0
fi

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
      echo "usage: $0 [--group language|std|protocols|cores|cores-components|cores-execution|cores-execution-datapath|rfpl] [--list-fixtures|--list-example-sources|--verify-only|--simulate-only|--golden-only|--full|--update-goldens]" >&2
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
  ""|language|std|protocols|cores|cores-components|cores-execution|cores-execution-datapath|rfpl) ;;
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
  dont-care
  nested-bundle aggregate-memory one-hot-aggregate priority-encoder
  uart-dpi chi-full-flits chi-router-composition
  )

test_tmp_dir="$(mktemp -d /tmp/rhodium-circt.XXXXXX)"
trap 'rm -rf "$test_tmp_dir"' EXIT

if [[ "$mode" != --list-fixtures && "$mode" != --list-example-sources ]]; then
  circt_opt="${CIRCT_OPT:-$repo_dir/.tools/firtool-1.155.0/bin/circt-opt}"
  if [[ ! -x "$circt_opt" ]]; then
    if command -v circt-opt >/dev/null 2>&1; then
      circt_opt="$(command -v circt-opt)"
    else
      echo "circt-opt not found; run 'moon run rhodium:setup-circt' or set CIRCT_OPT" >&2
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
  local wanted="$1" group="$2" member
  case "$group" in
    cores) fixture_in_group "$wanted" cores-components || fixture_in_group "$wanted" cores-execution-datapath; return ;;
    cores-execution) group=cores-execution-datapath ;;
  esac
  for member in "${group_members[@]}"; do
    [[ "$member" == "$group|$wanted" ]] && return 0
  done
  return 1
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
  source "$repo_dir/tools/build/paths.sh"
  compilation_path_flags --build "$test_tmp_dir"
  if ! { time verilator "$@" -CFLAGS "$path_flags_shell" > "$build_log" 2>&1; }; then
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
        dpi_sources+=("$repo_dir/sims/cosim/rv5stage/rv5stage-adapter.cc" "$repo_dir/sims/cosim/vector/vector-adapter.cc")
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
    if grep -Eq '^module (RV5StageFrontend|VectorExecution)[ (_]' "$verilog"; then
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

fixture_groups=(language std protocols cores-components cores-execution-datapath rfpl)
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
