# SPDX-License-Identifier: Apache-2.0
"""Check conditional execution counts, mux/effect semantics, and failed-edge retries."""
import os
import random
import re
import shlex


def stimuli():
    rng = random.Random(61006)
    rows, expected = [], []
    q = peer = carried = x = y = 0
    mask = (1 << 17) - 1
    cases = [(1, reset, enable, select, a, b)
             for reset in (0, 1) for enable in (0, 1) for select in range(4)
             for a, b in ((0, 0), (mask, 1), (mask, mask), (13, 21))]
    cases += [(rng.randrange(2), int(rng.randrange(9) == 0), rng.randrange(2),
               rng.randrange(4), rng.getrandbits(17), rng.getrandbits(17))
              for _ in range(500)]
    for tick, reset, enable, select, a, b in cases:
        shared = (a + b) & mask
        if tick:
            old_q = q
            if shared < b:
                carried = (~a) & mask
            if reset:
                q = peer = 0
                x, y = a, b
            elif enable:
                if select == 1:
                    product = a * b & mask
                    q = (product + (product >> 3) + old_q) & mask
                elif select == 2:
                    q = (~b) & mask
                peer = (~shared) & mask
                x, y = (old_q + a) & mask, (~b) & mask
        rows.append([tick, reset, enable, select, a, b])
        expected.append([q, peer, carried, x, y, shared])
    return '\n'.join(' '.join(f'{v:x}' for v in row) for row in rows) + '\n', expected


def driver(native):
    model = 'rsim_pRsimBranches::Model' if native else 'VRsimBranches'
    header = 'RsimBranches.hpp' if native else 'VRsimBranches.h'
    inputs = ['reset', 'enable', 'select', 'a', 'b']
    outputs = ['q', 'peer', 'carried', 'x', 'y', 'shared']
    def inp(name):
        return 'dut.inputs.p' + name if native else 'dut.' + name
    def out(name):
        return 'dut.outputs().p' + name if native else 'dut.' + name
    assignments = ' '.join(f'{inp(name)} = {name};' for name in inputs)
    observation = " << ' ' << ".join(out(name) for name in outputs)
    step = 'dut.tick();' if native else 'dut.clock = 0; dut.eval(); dut.clock = 1; dut.eval();'
    before = 'const auto before = rsim_branch_evaluations;' if native else 'dut.clock = 0; dut.eval();'
    count_check = '''if (rsim_branch_evaluations - before != unsigned(tick && !reset && enable && select == 1))
      throw std::logic_error("inactive register datapath executed");''' if native else ''
    retry = r'''
  // Failed attempts dirty the inactive bank; disabled retries must still hold.
  for (unsigned parity = 0; parity < 2; ++parity) {
    const auto old = dut.outputs().pq;
    dut.inputs.preset = 0; dut.inputs.penable = 1; dut.inputs.pselect = 1;
    dut.inputs.pa = 111; dut.inputs.pb = 333; dut.inputs.pallow = 0;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
      bool rejected = false;
      try { dut.tick(); } catch (const std::runtime_error&) { rejected = true; }
      if (!rejected || dut.outputs().pq != old) return 3;
      dut.eval();
      if (dut.outputs().pq != old) return 4;
    }
    dut.inputs.pallow = 1; dut.inputs.penable = 0; dut.tick();
    if (dut.outputs().pq != old) return 5;
    auto copied = dut;
    copied.inputs.penable = 1; copied.tick();
    if (copied.outputs().pq != ((111 * 333 + ((111 * 333) >> 3) + old) & 131071) || dut.outputs().pq != old) return 6;
  }
''' if native else ''
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "{header}"
#include <iostream>
#include <stdexcept>
#include <cstdint>
{'unsigned rsim_branch_evaluations = 0;' if native else ''}
int main() {{
  {model} dut;
  {inp('allow')} = 1;
  std::uint64_t tick, reset, enable, select, a, b;
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> tick >> reset >> enable >> select >> a >> b) {{
    {before}
    {assignments}
    dut.eval(); dut.eval();
    if (tick) {{ {step} }}
    {count_check}
    std::cout << {observation} << '\\n';
  }}
{retry}
}}
'''


def run_suite(work, run, compare, differential):
    # Observe execution of the fixture's unique multiplication. This checks the
    # optimization itself, including inactive/reset/eval paths, without adding
    # diagnostics or counters to the production simulator interface.
    original = (work / 'RsimBranches.cpp').read_text()
    pattern = r'(?m)^(\s*\[\[maybe_unused\]\] const std::uint64_t v\d+ = [^\n]*v\d+ \* v\d+[^\n]*;)$'
    instrumented, count = re.subn(pattern, r'  ++rsim_branch_evaluations;\n\1', original)
    if count != 1:
        raise AssertionError(f'expected one capture multiplication, got {count}')
    source = work / 'branches-instrumented.cpp'
    source.write_text('extern unsigned rsim_branch_evaluations;\n' + instrumented)
    main = work / 'branches-main.cpp'
    main.write_text(driver(True))
    run(shlex.split(os.environ.get('CXX', 'c++')) +
        ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
         str(source), str(main), '-o', str(work / 'branches-native')], work, 'branches-build')
    stimulus, expected = stimuli()
    compare(run([str(work / 'branches-native')], work, 'branches-native', stimulus), expected, 'rsim branches')
    if differential:
        reference = work / 'branches-reference.cpp'
        reference.write_text(driver(False))
        run(['verilator', '--cc', '--exe', '--build', '--assert', '-j', '2',
             '--top-module', 'RsimBranches', '--Mdir', str(work / 'branches-obj'),
             str(work / 'RsimBranches.sv'), str(reference)], work, 'branches-verilator')
        compare(run([str(work / 'branches-obj/VRsimBranches')], work, 'branches-reference', stimulus), expected, 'SV branches')
    return len(expected)


def conditional_stimuli():
    rng = random.Random(61007)
    mask = (1 << 32) - 1
    rows, expected = [], []
    memory = [None] * 4
    captured = call_result = 0
    read_result = (0, 0)
    # Initialize all words before probing synchronous old-state reads.
    cases = [(1, 0, 0, 1, 0, 0, 0, index, *([index + 10] * 8)) for index in range(4)]
    cases += [(tick, reset, select, we, re, ce, ae, 0, *([0xffffffff] * 8))
              for tick in (0, 1) for reset in (0, 1) for select in range(4)
              for we, re, ce, ae in ((0, 0, 0, 0), (1, 1, 1, 1), (1, 0, 1, 0), (0, 1, 0, 1))]
    cases += [(rng.randrange(2), int(rng.randrange(9) == 0), rng.randrange(4),
               *(rng.randrange(2) for _ in range(4)), rng.randrange(4),
               *(rng.getrandbits(32) for _ in range(8))) for _ in range(400)]
    for row in cases:
        tick, reset, select, we, re, ce, ae, index, *raw = row
        values = [(v * 3) & mask for v in raw]
        selected = values[0] if select == 1 else values[2] if select == 2 else values[1]
        if tick:
            captured = 17 if reset else (~selected & mask)
            if ce:
                argument = raw[4] if select == 1 else values[4]
                call_result = (2 * argument + 5) & mask
            read_result = memory[values[6] & 3] if re else None
            if read_result is None:
                read_result = (0, 0)
            if we:
                memory[index] = values[3]
        rows.append([*row, raw[4], values[5]])
        expected.append([(selected + values[2]) & mask, ~selected & mask, values[2],
                         captured, read_result, call_result, ~values[7] & mask if select == 1 else (values[7] + 1) & mask])
    return '\n'.join(' '.join(f'{v:x}' for v in row) for row in rows) + '\n', expected


CONDITIONAL_INPUTS = ['reset', 'select', 'write_enable', 'read_enable', 'call_enable', 'check_enable',
                      'index', 'comb_data', 'default_data', 'shared_data', 'write_data',
                      'call_data', 'check_data', 'read_index', 'sibling_data', 'raw_call', 'expected']
CONDITIONAL_PROBES = ['comb_data', 'default_data', 'shared_data', 'write_data', 'call_data', 'check_data', 'read_index', 'sibling_data']


def conditional_driver(native):
    def inp(name):
        return 'dut.inputs.p' + name.replace('_', '_u') if native else 'dut.' + name
    def out(name):
        return 'dut.outputs().p' + name.replace('_', '_u') if native else 'dut.' + name
    assignments = ' '.join(f'{inp(name)} = {name};' for name in CONDITIONAL_INPUTS)
    outputs = ['combined', 'inverted', 'shared', 'captured', 'read_data', 'call_result', 'sibling']
    observes = " << ' ' << ".join(out(name) for name in outputs)
    checks = '''
    const unsigned evals = 2 + 2 * unsigned(tick);
    const std::array<unsigned, 8> expected_counts = {evals * (select == 1), evals * (select != 1 && select != 2), evals,
      unsigned(tick && write_enable), unsigned(tick && call_enable && select != 1), unsigned(tick && check_enable && !reset),
      unsigned(tick && read_enable), evals};
    if (rsim_conditional_evaluations != expected_counts) throw std::logic_error("conditional execution count");
    if (foreign_calls - calls_before != unsigned(tick && call_enable)) throw std::logic_error("DPI enable");
''' if native else ''
    retry = '''
  dut.inputs.preset = 0; dut.inputs.pcheck_uenable = 1;
  dut.inputs.pcheck_udata = 1; dut.inputs.pexpected = 0;
  dut.inputs.pwrite_uenable = 1; dut.inputs.pcall_uenable = 1;
  const auto before = dut.outputs().pcaptured;
  const auto calls_before = foreign_calls;
  bool rejected = false;
  try { dut.tick(); } catch (const std::runtime_error&) { rejected = true; }
  if (!rejected || dut.outputs().pcaptured != before || foreign_calls != calls_before) return 7;
  dut.inputs.pcheck_uenable = 0; dut.inputs.pwrite_uenable = 0; dut.inputs.pcall_uenable = 0;
  dut.tick();
  if (foreign_calls != calls_before) return 8;
''' if native else ''
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "{'RsimConditionals.hpp' if native else 'VRsimConditionals.h'}"
#include <array>
#include <iostream>
#include <stdexcept>
#include <cstdint>
{'std::array<unsigned, 8> rsim_conditional_evaluations{};' if native else ''}
static unsigned foreign_calls = 0;
extern "C" unsigned rsim_conditional_call(unsigned value, unsigned again) {{ ++foreign_calls; return value + again + 5; }}
int main() {{
  {'rsim_pRsimConditionals::Model' if native else 'VRsimConditionals'} dut;
  std::uint64_t tick, {', '.join(CONDITIONAL_INPUTS)};
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> tick >> {' >> '.join(CONDITIONAL_INPUTS)}) {{
    {'rsim_conditional_evaluations.fill(0); const auto calls_before = foreign_calls;' if native else 'dut.clock = 0; dut.eval();'}
    {assignments}
    dut.eval(); dut.eval();
    if (tick) {{ {'dut.tick();' if native else 'dut.clock = 1; dut.eval();'} }}
    {checks}
    std::cout << {observes} << '\\n';
  }}
  {retry}
}}
'''


def run_conditional_suite(work, run, compare, differential):
    original = (work / 'RsimConditionals.cpp').read_text()
    for index, name in enumerate(CONDITIONAL_PROBES):
        # Input identities survive schedule renumbering and helper splitting.
        port = re.escape('inputs.p' + name.replace('_', '_u'))
        ids = set(re.findall(r'v(\d+) = \(' + port + r'\)', original))
        crossing = (r'(scratch\.g\d+\[\d+\]) = \(' + port +
                    r'\)[^\n]*;\n\s*\[\[maybe_unused\]\] const auto& v(\d+) = \1;')
        ids.update(value for _, value in re.findall(crossing, original))
        if len(ids) != 1:
            raise AssertionError(f'cannot identify input producer for {name}: {ids}')
        value = next(iter(ids))
        pattern = r'(?m)^([^\n]*\(v' + value + r' \* v\d+\)[^\n]*;)$'
        original, count = re.subn(pattern, rf'  ++rsim_conditional_evaluations[{index}];\n\1', original)
        if count == 0:
            raise AssertionError(f'no computation probe for {name}')
    source = work / 'conditionals-instrumented.cpp'
    source.write_text('#include <array>\nextern std::array<unsigned, 8> rsim_conditional_evaluations;\n' + original)
    main = work / 'conditionals-main.cpp'
    main.write_text(conditional_driver(True))
    run(shlex.split(os.environ.get('CXX', 'c++')) +
        ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all', str(source), str(main), '-o', str(work / 'conditionals-native')], work, 'conditionals-build')
    stimulus, expected = conditional_stimuli()
    compare(run([str(work / 'conditionals-native')], work, 'conditionals-native', stimulus), expected, 'rsim conditionals')
    if differential:
        reference = work / 'conditionals-reference.cpp'
        reference.write_text(conditional_driver(False))
        run(['verilator', '--cc', '--exe', '--build', '--assert', '-j', '2', '--top-module', 'RsimConditionals',
             '--Mdir', str(work / 'conditionals-obj'), str(work / 'RsimConditionals.sv'), str(reference)], work, 'conditionals-verilator')
        compare(run([str(work / 'conditionals-obj/VRsimConditionals')], work, 'conditionals-reference', stimulus), expected, 'SV conditionals')
    return len(expected)
