# SPDX-License-Identifier: Apache-2.0
"""Check wide DPI effects, ABI buffers, and held state against a Python oracle."""
import argparse
import os
from pathlib import Path
import random
import shlex
import shutil
import tempfile

WIDTHS = (65, 129, 512, 513)
SIDES = ('first', 'second')
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]


def encoded(name):
    return 'p' + name.replace('_', '_u')


def words(value, width):
    return [None if value is None else (value >> bit) & 0xffffffff
            for bit in range(0, width, 32)]


def ports():
    return [(f'{side}_{prefix}{kind}{width}', bits)
            for side in SIDES for width in WIDTHS
            for kind, bits in (('wide', width), ('native', 16), ('returned', 5))
            for prefix in ('', 'sample_')] + [
                (f'{side}_partial{width}', 5) for side in SIDES for width in WIDTHS]


def host_source():
    functions = []
    for width in WIDTHS:
        n = (width + 31) // 32
        mask = (1 << (width % 32 or 32)) - 1
        functions.append(f'''
extern "C" std::uint32_t rsim_wide{width}(char tag, const std::uint32_t* data,
                                        std::uint32_t* wide, short* native) {{
  if ((data[{n - 1}] & ~UINT32_C({mask})) != 0) throw std::runtime_error("input padding");
  const auto id = static_cast<unsigned char>(tag);
  for (unsigned i = 0; i < {n}; ++i) {{
    events.push_back({{{width}, id, i, data[i]}});
    // Deliberately dirty final-limb padding; the adapter must discard it.
    wide[i] = ~data[i];
  }}
  const auto bits = static_cast<unsigned short>(data[0] ^ data[{n - 1}] ^ id);
  std::memcpy(native, &bits, sizeof(bits));
  return (data[0] + data[{n - 1}] + id) | ~UINT32_C(31);
}}
extern "C" void rsim_wide_observe{width}(char tag, const std::uint32_t* data) {{
  if ((data[{n - 1}] & ~UINT32_C({mask})) != 0) throw std::runtime_error("held padding");
  for (unsigned i = 0; i < {n}; ++i)
    events.push_back({{{1000 + width}, static_cast<unsigned char>(tag), i, data[i]}});
}}
''')
    return '''// SPDX-License-Identifier: Apache-2.0
#include "wide-foreign-host.hpp"
#include <cstring>
#include <stdexcept>
#ifdef RSIM_REFERENCE
#include "VDpiWide__Dpi.h"
#endif
std::vector<Event> events;
extern "C" char rsim_wide_unused(char tag, std::uint32_t* wide) {
  for (unsigned i = 0; i < 17; ++i) wide[i] = UINT32_MAX - i;
  events.push_back({0, static_cast<unsigned char>(tag), 0, 0});
  return 7;
}
''' + ''.join(functions)


def stimuli():
    rng = random.Random(58139)
    held = {(side, width): [None] * 3 for side in SIDES for width in WIDTHS}
    samples = {key: [None] * 3 for key in held}
    partial = {key: None for key in held}
    rows, expected = [], []
    for cycle in range(300):
        tick = int(cycle != 0 and cycle % 7 != 4)
        reset = int(cycle == 1 or cycle % 23 == 0)
        enables = [1, 1] if cycle == 1 else [rng.randrange(2), rng.randrange(2)]
        monitor = rng.randrange(2)
        row = [tick, reset, *enables, monitor]
        effects = []
        for side, enabled, tag in zip(SIDES, enables, (1, 2)):
            for width in WIDTHS:
                mask = (1 << width) - 1
                patterns = (0, mask, 1, 1 << (width - 1), (1 << 64) - 1,
                            1 << 64, 1 << 32, mask ^ (1 << (width - 1)))
                data = patterns[(cycle + tag) % len(patterns)] if cycle < 40 else rng.getrandbits(width)
                raw = words(data, width)
                if cycle % 2 and width % 32:
                    raw[-1] |= 0xffffffff ^ ((1 << (width % 32)) - 1)
                row += raw
                key = (side, width)
                old = held[key]
                if tick:
                    samples[key] = [0] * 3 if reset else list(old)
                    if enabled:
                        arg = data if reset else data ^ old[0]
                        for kind, identity, value in ((width, tag, arg), (width, tag + 10, data),
                                                      (1000 + width, tag, old[0])):
                            effects += [(kind, identity, i, v) for i, v in enumerate(words(value, width))]
                        low, high = arg & 0xffffffff, arg >> (32 * ((width - 1) // 32))
                        held[key] = [(~arg) & mask, (low ^ high ^ tag) & 65535, (low + high + tag) & 31]
                        partial[key] = ((data & 0xffffffff) + (data >> (32 * ((width - 1) // 32))) + tag + 10) & 31
        if tick and monitor:
            effects.append((0, 0, 0, 0))
        values = []
        for side in SIDES:
            for width in WIDTHS:
                for index, bits in enumerate((width, 16, 5)):
                    values += words(held[side, width][index], bits)
                    values += words(samples[side, width][index], bits)
        values += [partial[side, width] for side in SIDES for width in WIDTHS]
        effects.sort(key=lambda event: event[:3])
        expected.append(values + [len(effects)] + [v for event in effects for v in event])
        rows.append(' '.join(f'{value:x}' for value in row))
    return '\n'.join(rows) + '\n', expected


def driver(native):
    model = 'rsim_pDpiWide::Model' if native else 'VDpiWide'
    input_ref = lambda name: f'dut.inputs.{encoded(name)}' if native else f'dut.{name}'
    output_ref = lambda name: f'dut.outputs().{encoded(name)}' if native else f'dut.{name}'
    assignments = [f'{input_ref(name)} = {var};' for name, var in
                   (('reset', 'reset'), ('first_enable', 'first'), ('second_enable', 'second'),
                    ('monitor_enable', 'monitor'))]
    for side in SIDES:
        for width in WIDTHS:
            for i in range((width + 31) // 32):
                mask = (1 << (width % 32)) - 1 if i == width // 32 and width % 32 else 0xffffffff
                target = input_ref(f'{side}_data{width}') + ('.words' if native else '') + f'[{i}]'
                assignments.append(f'std::cin >> word; {target} = word' + (';' if native else f' & UINT32_C({mask});'))
    values = []
    for name, width in ports():
        ref = output_ref(name)
        if width > 64:
            values += [ref + ('.words' if native else '') + f'[{i}]' for i in range((width + 31) // 32)]
        else:
            values.append(f'static_cast<std::uint32_t>({ref})')
    snapshot = ', '.join(values)
    step = 'dut.tick();' if native else 'dut.clock = 1; dut.eval(); dut.clock = 0; dut.eval();'
    extra = extra_end = ''
    if native:
        other_values = snapshot.replace('dut.', 'other.')
        extra = f'''
  rsim_pDpiWide::Model other;
  other.inputs.preset = other.inputs.pfirst_uenable = other.inputs.psecond_uenable = 1;
  other.tick();
  const auto other_snapshot = [&]() {{ return std::array<std::uint32_t, {len(values)}>{{{other_values}}}; }};
  const auto other_before = other_snapshot();
  // No scheduled wide value exists in this model: only an unused DPI out.
  rsim_pDpiUnusedWide::Model unused;
  unused.inputs.penable = 1;
  events.clear(); unused.eval();
  if (!events.empty()) return 9;
  unused.tick();
  if (events.size() != 1) return 10;
  unused.inputs.penable = 0; events.clear(); unused.tick();
  if (!events.empty()) return 11;
'''
        count = 6 * sum((w + 31) // 32 for w in WIDTHS) + 1
        extra_end = f'''
  dut.inputs.preset = 0;
  dut.inputs.pfail_uguard = dut.inputs.pfirst_uenable = dut.inputs.psecond_uenable = dut.inputs.pmonitor_uenable = 1;
  dut.eval();
  const auto before = snapshot(); events.clear();
  bool caught = false;
  try {{ dut.tick(); }} catch (const std::runtime_error&) {{ caught = true; }}
  if (!caught || !events.empty() || snapshot() != before) return 4;
  dut.eval();
  if (!events.empty() || snapshot() != before) return 5;
  dut.inputs.pfail_uguard = 0; dut.tick();
  if (events.size() != {count}) return 6;
  other.eval();
  if (other_snapshot() != other_before) return 7;
'''
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "{'DpiWide.hpp' if native else 'VDpiWide.h'}"
{'#include "DpiUnusedWide.hpp"' if native else ''}
#include "wide-foreign-host.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
int main() {{
  {model} dut;
  {input_ref('fail_guard')} = 0;
  {'dut.clock = 0; dut.eval();' if not native else ''}
  {extra}
  const auto snapshot = [&]() {{ return std::array<std::uint32_t, {len(values)}>{{{snapshot}}}; }};
  unsigned tick, reset, first, second, monitor;
  std::uint32_t word;
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> tick >> reset >> first >> second >> monitor) {{
    events.clear();
    {chr(10).join(assignments)}
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
    work = work / 'wide-foreign'
    work.mkdir()
    run([str(ROOT / 'tools/run-racket.sh'), str(HERE / 'emit-wide-foreign.rhm'),
         str(work), 'differential' if differential else 'native'], work, 'emit')
    host = work / 'wide-foreign-host.cpp'
    host.write_text(host_source())
    (work / 'wide-foreign-host.hpp').write_text('''// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <vector>
using Event = std::array<std::uint32_t, 4>;
extern std::vector<Event> events;
''')
    main = work / 'main.cpp'
    main.write_text(driver(True))
    run(shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all', str(work / 'DpiWide.cpp'), str(work / 'DpiUnusedWide.cpp'),
        str(host), str(main), '-o', str(work / 'native')], work, 'build')
    vectors, expected = stimuli()
    compare(run([str(work / 'native')], work, 'native', vectors), expected, 'wide foreign native')
    if differential:
        main = work / 'reference.cpp'
        main.write_text(driver(False))
        run(['verilator', '--cc', '--exe', '--build', '--assert', '-j', '2', '--top-module', 'DpiWide',
             '-CFLAGS', '-DRSIM_REFERENCE', '--Mdir', str(work / 'obj'), str(work / 'DpiWide.sv'),
             str(host), str(main)], work, 'verilator')
        compare(run([str(work / 'obj/VDpiWide')], work, 'reference', vectors), expected, 'wide foreign SV')
    return len(expected)


def main():
    from run import run, compare
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--differential', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='rhodium-rsim-dpi-'))
    try:
        count = run_suite(work, run, compare, args.differential)
        print(f'rsim: {count} wide foreign observations passed' + (' on both backends' if args.differential else ''))
    except BaseException:
        print(f'rsim artifacts and logs retained at {work}')
        raise
    else:
        shutil.rmtree(work)


if __name__ == '__main__':
    main()
