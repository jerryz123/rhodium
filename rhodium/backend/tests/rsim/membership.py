# SPDX-License-Identifier: Apache-2.0
"""Check authored membership tables and dynamic alternatives against an integer oracle."""
import argparse
import os
from pathlib import Path
import random
import shlex
import shutil
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
MASK = (1 << 64) - 1


def stimuli():
    rng = random.Random(7331)
    rows, expected = [], []
    held = 0
    values = list(range(80)) + [1 << 63, (1 << 63) - 1, MASK, MASK - 1]
    values += [rng.getrandbits(64) for _ in range(160)]
    for index, value in enumerate(values):
        for high in (0, 1):
            tick = int(index % 3 != 1)
            reset = int(index == 0 or index % 31 == 0)
            enable = int(index % 4 != 2)
            other = value if index % 2 else rng.getrandbits(64)
            low = value
            # Native input normalization must discard padding above bit 64.
            dirty_high = (rng.getrandbits(32) & ~1) | high
            wide = low | high << 64
            small = value & 31
            flags = [value in {0, 1, 7, 63, 1 << 63, MASK},
                     small in {0, 3, 5, 13, 31}, small in range(8, 24),
                     wide in {0, 1, 1 << 64, (1 << 65) - 1},
                     value == other or value == 7, False, value == 3,
                     bool(value & 1), value in {MASK, 1 << 63, 7},
                     value & 3 in {0, 3}]
            result = sum(int(flag) << bit for bit, flag in enumerate(flags))
            if tick:
                held = 0 if reset else result if enable else held
            rows.append([tick, reset, enable, value, other, low, dirty_high])
            expected.append([result, held])
    return '\n'.join(' '.join(f'{x:x}' for x in row) for row in rows) + '\n', expected


def driver(native):
    model = 'rsim_pRsimMembership::Model' if native else 'VRsimMembership'
    header = 'RsimMembership.hpp' if native else 'VRsimMembership.h'
    def port(name):
        return 'dut.inputs.p' + name if native else 'dut.' + name
    wide = port('wide') + ('.words' if native else '')
    outputs = ['dut.outputs().p' + name if native else 'dut.' + name
               for name in ('result', 'sampled')]
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "{header}"
#include <cstdint>
#include <iostream>
int main() {{
  {model} dut;
  std::uint64_t tick, reset, enable, value, other, low, high;
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> tick >> reset >> enable >> value >> other >> low >> high) {{
    {'' if native else 'dut.clock = 0; dut.eval();'}
    {port('reset')} = reset; {port('enable')} = enable;
    {port('value')} = value; {port('other')} = other;
    {wide}[0] = std::uint32_t(low); {wide}[1] = std::uint32_t(low >> 32);
    {wide}[2] = std::uint32_t(high{'' if native else ' & 1'});
    dut.eval(); dut.eval();
    if (tick) {{ {'dut.tick();' if native else 'dut.clock = 1; dut.eval();'} }}
    std::cout << std::uint64_t({outputs[0]}) << ' ' << std::uint64_t({outputs[1]}) << '\\n';
  }}
}}
'''


def run_suite(work, run, compare, differential):
    emit = [str(ROOT / 'tools/run-racket.sh'), str(HERE / 'emit-membership.rhm'), str(work)]
    run(emit + ['rsim'], work, 'membership-emit')
    source = (work / 'RsimMembership.cpp').read_text()
    if 'switch (' not in source:
        raise AssertionError('constant membership did not use native table selection')
    native = work / 'membership-native.cpp'
    native.write_text(driver(True))
    run([*shlex.split(os.environ.get('CXX', 'c++')), '-std=c++17', '-O2',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
         str(work / 'RsimMembership.cpp'), str(native), '-o', str(work / 'membership')],
        work, 'membership-build')
    vectors, expected = stimuli()
    compare(run([str(work / 'membership')], work, 'membership-native', vectors), expected, 'membership')
    if differential:
        run(emit + ['verilog'], work, 'membership-sv-emit')
        sv_driver = work / 'membership-sv.cpp'
        sv_driver.write_text(driver(False))
        run(['verilator', '--cc', '--exe', '--build', '-j', '2', '--top-module', 'RsimMembership',
             '--Mdir', str(work / 'membership-obj'), str(work / 'RsimMembership.sv'), str(sv_driver)],
            work, 'membership-verilator')
        compare(run([str(work / 'membership-obj/VRsimMembership')], work, 'membership-sv', vectors),
                expected, 'membership SV')
    return len(expected)


if __name__ == '__main__':
    import run as runner
    parser = argparse.ArgumentParser()
    parser.add_argument('--differential', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='rhodium-membership-'))
    try:
        count = run_suite(work, runner.run, runner.compare, args.differential)
        print(f'{count} membership observations passed')
    except BaseException:
        print(f'membership artifacts retained at {work}')
        raise
    else:
        shutil.rmtree(work)
