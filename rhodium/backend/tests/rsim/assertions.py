# SPDX-License-Identifier: Apache-2.0
"""Check tick-only assertions, source diagnostics, and native failure atomicity."""
import os
import re
import shlex
import subprocess

# Shared stimuli intentionally use only defined, initialized state and avoid
# read/write collisions. The SV process stops on failure; native recovery and
# rollback are additional rsim API guarantees checked after catching the error.
SCENARIOS = {
    0: None,
    1: ('first', 'named_check', 'assert-fixture.rhdl:30:2'),
    2: ('second', 'named_check', 'assert-fixture.rhdl:30:2'),
    3: ('first', None, 'dir/"quoted"\\checks\nline.rhdl:41:7'),
    4: ('first', 'sample_check', 'assert-fixture.rhdl:42:0'),
    5: ('second', None, 'dir/"quoted"\\checks\nline.rhdl:41:7'),
    6: ('first', 'named_check', 'assert-fixture.rhdl:30:2'),
    7: None,
    8: ('monitor', 'monitor_check', 'monitor.rhdl:9:1'),
    9: ('', None, 'top.rhdl:70:3'),
    10: None,
}


def driver(native):
    header = 'RsimAssertions.hpp' if native else 'VRsimAssertions.h'
    model = 'rsim_pRsimAssertions::Model' if native else 'VRsimAssertions'
    step = 'dut.tick();' if native else 'dut.clock = 0; dut.eval(); dut.clock = 1; dut.eval(); dut.clock = 0; dut.eval();'
    snapshot = ', '.join(f'O({side}_{name})' for side in ('first', 'second')
                         for name in ('state', 'async_data', 'sync_data', 'array0', 'array1'))
    body = r'''
  I(first_reset) = I(second_reset) = 1;
  I(first_guard) = I(second_guard) = 1;
  I(first_unnamed_guard) = I(second_unnamed_guard) = 1;
  I(first_sample_guard) = I(second_sample_guard) = 1;
  I(first_write_enable) = I(second_write_enable) = 1;
  I(first_write_data) = 11; I(second_write_data) = 17;
  step();
  require(O(first_state) == 0 && O(second_state) == 0, "reset registers");
  require(O(first_array0) == 0 && O(first_array1) == 0 && O(second_array0) == 0 && O(second_array1) == 0, "reset arrays");
  I(first_write_address) = I(second_write_address) = 1;
  I(first_write_data) = 23; I(second_write_data) = 29;
  I(first_read_enable) = I(second_read_enable) = 1;
  step();
  require(O(first_sync_data) == 11 && O(second_sync_data) == 17, "initialize stored reads");
  I(first_reset) = I(second_reset) = 0;
  I(first_guard) = I(second_guard) = 0;
  I(first_unnamed_guard) = I(second_unnamed_guard) = 0;
  I(first_sample_guard) = I(second_sample_guard) = 0;
  I(first_next_state) = 1; I(second_next_state) = 2;
  I(first_write_enable) = I(second_write_enable) = 0;
  step();
  require(O(first_state) == 1 && O(second_state) == 2, "guard suppression");
  I(first_guard) = I(second_guard) = 1;
  I(first_unnamed_guard) = I(second_unnamed_guard) = 1;
  I(first_sample_guard) = I(second_sample_guard) = 1;
  I(first_condition) = I(second_condition) = 1;
  I(first_unnamed_condition) = I(second_unnamed_condition) = 1;
  I(first_expected_state) = 1; I(second_expected_state) = 2;
  I(first_next_state) = 3; I(second_next_state) = 4;
  I(first_write_enable) = I(second_write_enable) = 1;
  I(first_write_data) = 31; I(second_write_data) = 37;
  step();
  require(O(first_state) == 3 && O(second_state) == 4, "pre-edge assertion sampling");
  require(O(first_array0) == 0 && O(first_array1) == 3 && O(second_array0) == 0 && O(second_array1) == 4, "array update preserves other lane");
  require(O(first_sync_data) == 11 && O(second_sync_data) == 17, "read latency");
  I(first_condition) = I(second_condition) = 0;
  I(first_unnamed_condition) = I(second_unnamed_condition) = 0;
  dut.eval(); dut.eval();
  require(O(first_state) == 3 && O(second_state) == 4, "eval preserves state");
  if (scenario == 10) { std::cout << "PASS eval-only\n"; return 0; }
  I(first_condition) = I(second_condition) = 1;
  I(first_unnamed_condition) = I(second_unnamed_condition) = 1;
  I(first_expected_state) = 3; I(second_expected_state) = 4;
  I(first_next_state) = 5; I(second_next_state) = 6;
  I(first_write_data) = 41; I(second_write_data) = 43;
  I(first_write_address) = I(second_write_address) = 0;
  I(first_read_address) = I(second_read_address) = 1;
  I(first_late_next) = I(second_late_next) = 1;
  switch (scenario) {
    case 1: I(first_condition) = 0; break;
    case 2: I(second_condition) = 0; break;
    case 3: I(first_unnamed_condition) = 0; break;
    case 4: I(first_expected_state) = 99; break;
    case 5: I(second_unnamed_condition) = 0; break;
    case 6: I(first_condition) = 0; I(first_late_mode) = 1; break;
    case 7: I(first_condition) = 0; I(first_late_mode) = 2; break;
    case 8: I(monitor_guard) = 1; break;
    case 9: I(top_guard) = 1; break;
  }
  dut.eval();
  std::cout << "ARM " << scenario << std::endl;
'''
    if native:
        body += r'''
  const auto before = snapshot();
  try { step(); }
  catch (const std::runtime_error& error) {
    require(snapshot() == before, "failed tick changed cached outputs");
    dut.eval();
    require(snapshot() == before, "failed tick changed state or asynchronous memory");
    // Dirty the same inactive bank again with different pending values.
    I(first_next_state) = 91; I(second_next_state) = 93;
    I(first_write_data) = 95; I(second_write_data) = 97;
    bool rejected_again = false;
    try { step(); } catch (const std::runtime_error&) { rejected_again = true; }
    require(rejected_again && snapshot() == before, "repeated failure published staged state");
    dut.eval();
    require(snapshot() == before, "eval observed a rejected bank");
    I(first_next_state) = 5; I(second_next_state) = 6;
    I(first_write_data) = 41; I(second_write_data) = 43;
    // Read the location the rejected edge would have written. This distinguishes
    // rollback of synchronous storage from rollback of its read-result register.
    I(first_guard) = I(second_guard) = 0;
    I(first_unnamed_guard) = I(second_unnamed_guard) = 0;
    I(first_sample_guard) = I(second_sample_guard) = 0;
    I(monitor_guard) = I(top_guard) = 0;
    I(first_write_enable) = I(second_write_enable) = 0;
    I(first_read_address) = I(second_read_address) = 0;
    step();
    require(O(first_sync_data) == 11 && O(second_sync_data) == 17, "failed tick wrote synchronous memory");
    require(O(first_async_data) == 31 && O(second_async_data) == 37, "failed tick wrote asynchronous memory");
    // One successful tick changes bank parity; failure must still preserve
    // both memories and stored read results before a second successful retry.
    I(top_guard) = 1;
    I(first_next_state) = 101; I(second_next_state) = 103;
    I(first_write_enable) = I(second_write_enable) = 1;
    I(first_write_data) = 107; I(second_write_data) = 109;
    const auto odd_before = snapshot();
    bool odd_rejected = false;
    try { step(); } catch (const std::runtime_error&) { odd_rejected = true; }
    require(odd_rejected && snapshot() == odd_before, "odd-parity failure changed outputs");
    dut.eval();
    require(snapshot() == odd_before, "odd-parity failure changed storage");
    I(top_guard) = 0;
    I(first_next_state) = 5; I(second_next_state) = 6;
    I(first_write_data) = 41; I(second_write_data) = 43;
    I(first_read_address) = I(second_read_address) = 1;
    step();
    require(O(first_state) == 5 && O(second_state) == 6, "recovery registers");
    require(O(first_array0) == 5 && O(first_array1) == 3 && O(second_array0) == 6 && O(second_array1) == 4, "array recovery");
    require(O(first_async_data) == 41 && O(second_async_data) == 43, "recovery writes");
    require(O(first_sync_data) == 31 && O(second_sync_data) == 37, "recovery read results");
    std::cout << "ROLLBACK_AND_RECOVERY_OK\n" << error.what() << std::endl;
    return 65;
  }
'''
    else:
        body += '  step();\n'
    body += r'''
  require(O(first_state) == 5 && O(second_state) == 6, "successful commit");
  require(O(first_array0) == 5 && O(first_array1) == 3 && O(second_array0) == 6 && O(second_array1) == 4, "array commit");
  require(O(first_async_data) == 41 && O(second_async_data) == 43, "successful writes");
  require(O(first_sync_data) == 31 && O(second_sync_data) == 37, "successful read results");
  std::cout << "PASS " << scenario << std::endl;
  return 0;
'''

    def port(match):
        kind, name = match.groups()
        if not native:
            return 'dut.' + name
        prefix = 'dut.inputs.' if kind == 'I' else 'dut.outputs().'
        return prefix + 'p' + name.replace('_', '_u')

    body = re.sub(r'\b(I|O)\((\w+)\)', port, body)
    snapshot = re.sub(r'\b(I|O)\((\w+)\)', port, snapshot)
    snapshot_function = f'auto snapshot = [&]() {{ return std::array<std::uint64_t, 10>{{{snapshot}}}; }};' if native else ''
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "{header}"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
static void require(bool condition, const char* message) {{
  if (!condition) throw std::logic_error(message);
}}
int main(int argc, char** argv) {{
  if (argc != 2) return 2;
  const int scenario = std::stoi(argv[1]);
  {model} dut;
  auto step = [&]() {{ {step} }};
  {snapshot_function}
{body}
}}
'''


def check_scenarios(executable, work, native):
    label = 'rsim' if native else 'verilator'
    for scenario, failure in SCENARIOS.items():
        result = subprocess.run([str(executable), str(scenario)], cwd=work,
                                capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        log = work / f'assert-{label}-{scenario}.log'
        log.write_text(output)
        if failure is None:
            valid = result.returncode == 0 and 'PASS ' in output
        else:
            side, name, location = failure
            valid = result.returncode != 0 and f'ARM {scenario}' in output and 'PASS ' not in output
            valid = valid and 'assertion failed' in output.lower()
            if name:
                valid = valid and name in output
            else:
                valid = valid and 'unlabeled' in output.lower()
            if native:
                path = 'RsimAssertions' + ('/' + side if side else '')
                valid = valid and result.returncode == 65 and 'ROLLBACK_AND_RECOVERY_OK' in output
                valid = valid and f'[{path}]' in output and location in output
            elif side:
                valid = valid and f'.{side}' in output
        if not valid:
            raise AssertionError(f'unexpected {label} assertion outcome in scenario {scenario}; see {log}')


def run_suite(work, run, differential):
    source = work / 'assert-main.cpp'
    source.write_text(driver(True))
    # C++ NDEBUG must not disable hardware assertions.
    run(shlex.split(os.environ.get('CXX', 'c++')) +
        ['-std=c++17', '-O0', '-DNDEBUG', '-Wall', '-Wextra', '-Werror',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
         str(work / 'RsimAssertions.cpp'), str(source), '-o', str(work / 'assert-native')], work, 'assert-build')
    check_scenarios(work / 'assert-native', work, True)
    if differential:
        reference = work / 'assert-reference.cpp'
        reference.write_text(driver(False))
        run(['verilator', '--cc', '--exe', '--build', '--assert', '-j', '2',
             '--top-module', 'RsimAssertions', '--Mdir', str(work / 'assert-obj'),
             str(work / 'RsimAssertions.sv'), str(reference)], work, 'assert-verilator')
        check_scenarios(work / 'assert-obj/VRsimAssertions', work, False)
    return len(SCENARIOS)
