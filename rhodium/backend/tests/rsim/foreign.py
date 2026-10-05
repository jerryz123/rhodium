# SPDX-License-Identifier: Apache-2.0
"""Compare native and packed foreign effects and held results with direct SV."""
import os
import random
import shlex

WIDTHS = (1, 8, 16, 32, 64)
PACKED_WIDTHS = (3, 5, 31, 33, 63)
GROUP_WIDTHS = (1, 8, 16, 32, 64, 16)
SIDES = ('first', 'second')


def encoded(name):
    return 'p' + name.replace('_', '_u')


def host_source():
    functions = []
    for width, ctype in zip(WIDTHS, ('unsigned char', 'char', 'short', 'int', 'long long')):
        mask = (1 << width) - 1
        functions.append(f'''
extern "C" {ctype} rsim_foreign{width}(char tag, {ctype} data) {{
  events.push_back({{{width}, static_cast<unsigned char>(tag), static_cast<std::uint64_t>(data) & UINT64_C({mask})}});
  const auto bits = static_cast<typename std::make_unsigned<{ctype}>::type>(~static_cast<std::uint64_t>(data) & UINT64_C({mask}));
  {ctype} result;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}}
extern "C" void rsim_observe{width}(char tag, {ctype} data) {{
  events.push_back({{{100 + width}, static_cast<unsigned char>(tag), static_cast<std::uint64_t>(data) & UINT64_C({mask})}});
}}
''')
    for width in PACKED_WIDTHS:
        return_width = width if width <= 32 else 5
        return_mask = (1 << return_width) - 1
        second = ' | (std::uint64_t(data[1]) << 32)' if width > 32 else ''
        high = 'inverted[1] = static_cast<std::uint32_t>((~input) >> 32);' if width > 32 else ''
        functions.append(f'''
extern "C" std::uint32_t rsim_packed{width}(char tag, const std::uint32_t* data,
                                         long long seed, std::uint32_t* inverted, short* native) {{
  const auto input = std::uint64_t(data[0]){second};
  if ((input >> {width}) != 0) throw std::runtime_error("nonzero packed input padding");
  const auto identity = static_cast<unsigned char>(tag);
  const auto salt = static_cast<std::uint64_t>(seed);
  events.push_back({{{300 + width}, identity, input}});
  events.push_back({{{400 + width}, identity, salt}});
  // Deliberately set every padding bit: only declared result bits may escape.
  inverted[0] = static_cast<std::uint32_t>(~input);
  {high}
  *native = native_bits<short>((salt >> 32) ^ input ^ identity);
  return static_cast<std::uint32_t>(input + salt + identity) | ~UINT32_C({return_mask});
}}
extern "C" void rsim_packed_observe{width}(char tag, const std::uint32_t* data) {{
  const auto input = std::uint64_t(data[0]){second};
  if ((input >> {width}) != 0) throw std::runtime_error("nonzero packed procedure padding");
  events.push_back({{{500 + width}, static_cast<unsigned char>(tag), input}});
}}
''')
    return '''// SPDX-License-Identifier: Apache-2.0
#include "foreign-host.hpp"
#include <cstring>
#include <type_traits>
#include <stdexcept>
#ifdef RSIM_REFERENCE
#include "VDpiNative__Dpi.h"
#endif
std::vector<Event> events;
template <typename T> static T native_bits(std::uint64_t value) {
  const auto bits = static_cast<typename std::make_unsigned<T>::type>(value);
  T result;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}
extern "C" short rsim_group(char tag, long long data, unsigned char* bit,
                           char* byte, short* half, int* word, long long* wide) {
  const auto input = static_cast<std::uint64_t>(data);
  const auto identity = static_cast<unsigned char>(tag);
  events.push_back({202, identity, input});
  *bit = input & 1;
  *byte = native_bits<char>((input >> 8) ^ identity ^ 0xa5);
  *half = native_bits<short>((input >> 16) ^ 0x8000);
  *word = native_bits<int>(~input);
  *wide = native_bits<long long>(input ^ UINT64_C(0x800000000000005a));
  return native_bits<short>(input + identity + 0x8001);
}
extern "C" void rsim_sink(char data) {
  events.push_back({200, 0, static_cast<unsigned char>(data)});
}
extern "C" char rsim_unused_result(char data) {
  events.push_back({201, 0, static_cast<unsigned char>(data)});
  return data;
}
''' + ''.join(functions)


def stimuli():
    rng = random.Random(837)
    # Begin without enabled functions so initial result bits remain uncared.
    cases = [(0, 0, 0, 0, 0, 0, 0), (1, 1, 1, 1, 1, 0, (1 << 64) - 1)]
    edges = (0, 1, 0x80, 0x8000, 0x80000000, 1 << 32, (1 << 32) - 1,
             0x123456789abcdef0, 1 << 62, 1 << 63, (1 << 64) - 1)
    cases += [(1, 0, 1, 1, 1, a, b) for a in edges for b in edges]
    cases += [(rng.randrange(2), int(i % 13 == 0), rng.randrange(2), rng.randrange(2),
               rng.randrange(2), rng.getrandbits(64), rng.getrandbits(64)) for i in range(250)]
    rows, expected = [], []
    held = [[None] * len(WIDTHS) for _ in SIDES]
    samples = [[None] * len(WIDTHS) for _ in SIDES]
    group_held = [[None] * len(GROUP_WIDTHS) for _ in SIDES]
    group_samples = [[None] * len(GROUP_WIDTHS) for _ in SIDES]
    partial = [None, None]
    packed_held = [[[None] * 3 for _ in PACKED_WIDTHS] for _ in SIDES]
    packed_samples = [[[None] * 3 for _ in PACKED_WIDTHS] for _ in SIDES]
    packed_partial = [[None] * len(PACKED_WIDTHS) for _ in SIDES]
    for cycle, (tick, reset, first, second, monitor, a, b) in enumerate(cases):
        # Group calls can hold while single-result calls run, and vice versa.
        group_enables = (1, 1) if cycle == 1 else ((cycle >> 1) & 1, (cycle >> 2) & 1)
        effects = []
        for side, (enabled, data) in enumerate(((first, a), (second, b))):
            for index, width in enumerate(WIDTHS):
                mask = (1 << width) - 1
                if tick:
                    samples[side][index] = 0 if reset else held[side][index]
                    if enabled:
                        argument = (data ^ held[side][index]) if width == 32 and not reset else data
                        effects += [(width, side + 1, argument & mask), (100 + width, side + 1, held[side][index])]
                        held[side][index] = (~argument) & mask
        for side, (enabled, data) in enumerate(((first, a), (second, b))):
            for index, width in enumerate(PACKED_WIDTHS):
                mask = (1 << width) - 1
                return_mask = (1 << (width if width <= 32 else 5)) - 1
                if tick:
                    previous = packed_held[side][index]
                    packed_samples[side][index] = [0] * 3 if reset else list(previous)
                    if enabled:
                        argument = data & mask if reset else (data ^ previous[0]) & mask
                        identity = side + 1
                        effects += [(300 + width, identity, argument), (400 + width, identity, data),
                                    (500 + width, identity, previous[0]),
                                    (300 + width, identity + 10, data & mask), (400 + width, identity + 10, data)]
                        packed_held[side][index] = [(~argument) & mask, ((data >> 32) ^ argument ^ identity) & 65535,
                                                   (argument + data + identity) & return_mask]
                        packed_partial[side][index] = ((data & mask) + data + identity + 10) & return_mask
        for side, data in enumerate((a, b)):
            if tick:
                group_samples[side] = [0] * len(GROUP_WIDTHS) if reset else list(group_held[side])
                if group_enables[side]:
                    previous = group_held[side]
                    argument = data if reset else data ^ previous[4] ^ previous[5]
                    identity = side + 1
                    effects += [(202, identity, argument), (202, identity + 10, data)]
                    group_held[side] = [argument & 1, ((argument >> 8) ^ identity ^ 0xa5) & 255,
                                        ((argument >> 16) ^ 0x8000) & 65535, (~argument) & 0xffffffff,
                                        argument ^ 0x800000000000005a, (argument + identity + 0x8001) & 65535]
                    partial[side] = (data + identity + 10 + 0x8001) & 65535
        if tick and monitor:
            effects += [(200, 0, a & 255), (201, 0, a & 255), (202, 0, a & 255)]
        outputs = [value for side in range(2) for index in range(len(WIDTHS))
                   for value in (held[side][index], samples[side][index])]
        outputs += [value for side in range(2) for index in range(len(GROUP_WIDTHS))
                    for value in (group_held[side][index], group_samples[side][index])]
        outputs += partial
        outputs += [value for side in range(2) for index in range(len(PACKED_WIDTHS)) for result in range(3)
                    for value in (packed_held[side][index][result], packed_samples[side][index][result])]
        outputs += [value for side in packed_partial for value in side]
        # Each (operation, occurrence) key is unique, so unknown initial bits
        # never participate in sorting and order between calls is unconstrained.
        effects.sort(key=lambda event: event[:2])
        expected.append(outputs + [len(effects)] + [v for event in effects for v in event])
        rows.append((tick, reset, first, second, monitor, *group_enables, a, b))
    return '\n'.join(' '.join(f'{v:x}' for v in row) for row in rows) + '\n', expected


def driver(native):
    model = 'rsim_pDpiNative::Model' if native else 'VDpiNative'
    header = 'DpiNative.hpp' if native else 'VDpiNative.h'
    inputs = lambda name: f'dut.inputs.{encoded(name)}' if native else f'dut.{name}'
    outputs = lambda name: f'dut.outputs().{encoded(name)}' if native else f'dut.{name}'
    step = 'dut.tick();' if native else 'dut.clock = 1; dut.eval(); dut.clock = 0; dut.eval();'
    assignments = [f'{inputs("reset")} = reset;', f'{inputs("first_enable")} = first;',
                   f'{inputs("second_enable")} = second;', f'{inputs("monitor_enable")} = monitor;',
                   f'{inputs("monitor_data")} = a;']
    assignments += [f'{inputs(side + "_data" + str(width))} = {arg};'
                    for side, arg in zip(SIDES, ('a', 'b')) for width in WIDTHS]
    assignments += [f'{inputs(side + "_group_enable")} = {control}; {inputs(side + "_group_data")} = {arg};'
                    for side, control, arg in zip(SIDES, ('group_first', 'group_second'), ('a', 'b'))]
    assignments += [f'{inputs(side + "_packed_seed")} = {arg};' for side, arg in zip(SIDES, ('a', 'b'))]
    names = [f'{side}_{kind}{width}' for side in SIDES for width in WIDTHS for kind in ('result', 'sample')]
    names += [f'{side}_group_{kind}{index}' for side in SIDES for index in range(len(GROUP_WIDTHS)) for kind in ('result', 'sample')]
    names += [f'{side}_partial_return' for side in SIDES]
    names += [f'{side}_packed_{prefix}{kind}{width}' for side in SIDES for width in PACKED_WIDTHS
              for kind in ('out', 'native', 'return') for prefix in ('', 'sample_')]
    names += [f'{side}_packed_partial{width}' for side in SIDES for width in PACKED_WIDTHS]
    snapshot = ', '.join(f'static_cast<std::uint64_t>({outputs(name)})' for name in names)
    extra = ''
    if native:
        extra = '''
  // Distinct model objects own independent held state; initialize it through
  // calls rather than relying on unspecified power-on values.
  rsim_pDpiNative::Model other;
  other.inputs.preset = 1;
  other.inputs.pfirst_uenable = other.inputs.psecond_uenable = 1;
  other.inputs.pfirst_ugroup_uenable = other.inputs.psecond_ugroup_uenable = 1;
  other.tick();
  other.inputs.pfirst_uenable = other.inputs.psecond_uenable = 0;
  other.inputs.pfirst_ugroup_uenable = other.inputs.psecond_ugroup_uenable = 0;
  other.eval();
  if (other.outputs().pfirst_uresult64 != UINT64_MAX) return 7;
  const auto other_snapshot = [&]() { return std::array<std::uint64_t, OTHER_SIZE>{OTHER_VALUES}; };
  const auto other_before = other_snapshot();
  events.clear();
'''
        extra_end = '''
  // A failed check suppresses every effect, including outputless children,
  // and preserves both ordinary registers and held foreign results.
  dut.inputs.preset = 0;
  for (unsigned parity = 0; parity < 2; ++parity) {
    dut.inputs.pfail_uguard = 1;
    dut.inputs.pfirst_uenable = dut.inputs.psecond_uenable = dut.inputs.pmonitor_uenable = 1;
    dut.inputs.pfirst_ugroup_uenable = dut.inputs.psecond_ugroup_uenable = 1;
    dut.eval();
    const auto before = snapshot();
    events.clear();
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
      ++dut.inputs.pfirst_udata64;
      bool caught = false;
      try { dut.tick(); } catch (const std::runtime_error&) { caught = true; }
      if (!caught || !events.empty() || snapshot() != before) return 4;
      dut.eval();
      if (!events.empty() || snapshot() != before) return 5;
    }
    dut.inputs.pfail_uguard = 0;
    dut.tick();
    if (events.size() != 77) return 6;
  }
  other.eval();
  if (other_snapshot() != other_before) return 8;
'''
    else:
        extra_end = ''
    extra = extra.replace('OTHER_SIZE', str(len(names))).replace('OTHER_VALUES', snapshot.replace('dut.', 'other.'))
    assignment_text = '\n    '.join(assignments)
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "{header}"
#include "foreign-host.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
int main() {{
  {model} dut;
  {inputs("fail_guard")} = 0;
  {'dut.clock = 0; dut.eval();' if not native else ''}
  {extra}
  const auto snapshot = [&]() {{ return std::array<std::uint64_t, {len(names)}>{{{snapshot}}}; }};
  std::uint64_t tick, reset, first, second, monitor, group_first, group_second, a, b;
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> tick >> reset >> first >> second >> monitor >> group_first >> group_second >> a >> b) {{
    events.clear();
    {assignment_text}
    dut.eval(); dut.eval();
    if (!events.empty()) return 2;
    if (tick) {{ {step} }}
    const auto count = events.size();
    dut.eval(); dut.eval();
    if (events.size() != count) return 3;
    for (auto value : snapshot()) std::cout << value << ' ';
    std::sort(events.begin(), events.end());
    std::cout << events.size();
    for (const auto& event : events) for (auto value : event) std::cout << ' ' << value;
    std::cout << '\\n';
  }}
  {extra_end}
}}
'''


def run_suite(work, run, compare, differential):
    host = work / 'foreign-host.cpp'
    host.write_text(host_source())
    (work / 'foreign-host.hpp').write_text('''// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <vector>
using Event = std::array<std::uint64_t, 3>;
extern std::vector<Event> events;
''')
    native = work / 'foreign-main.cpp'
    native.write_text(driver(True))
    run(shlex.split(os.environ.get('CXX', 'c++')) +
        ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all', str(work / 'DpiNative.cpp'), str(native), str(host),
         '-o', str(work / 'foreign-native')], work, 'foreign-build')
    vectors, expected = stimuli()
    compare(run([str(work / 'foreign-native')], work, 'foreign-native', vectors), expected, 'foreign native')
    if differential:
        reference = work / 'foreign-reference.cpp'
        reference.write_text(driver(False))
        run(['verilator', '--cc', '--exe', '--build', '--assert', '-j', '2', '--top-module', 'DpiNative',
             '-CFLAGS', '-DRSIM_REFERENCE', '--Mdir', str(work / 'foreign-obj'),
             str(work / 'DpiNative.sv'), str(reference), str(host)], work, 'foreign-verilator')
        compare(run([str(work / 'foreign-obj/VDpiNative')], work, 'foreign-reference', vectors), expected, 'foreign Verilator')
    import wide_foreign
    return len(expected) + wide_foreign.run_suite(work, run, compare, differential)
