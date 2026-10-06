# SPDX-License-Identifier: Apache-2.0
"""Check authored mask indexing, overshifts, and capture against an integer oracle."""
import argparse
import os
from pathlib import Path
import random
import shlex
import shutil
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
WIDTHS = (1, 3, 31, 32, 63, 64, 65, 129)
MASK64 = (1 << 64) - 1


def stimuli():
    rng = random.Random(1109)
    indices = list(range(131)) + [255, 1 << 32, 1 << 63, MASK64]
    cases = [(value, index, high) for value in (0, (1 << 129) - 1, 1, 1 << 128)
             for index in indices for high in (0, 1)]
    cases += [(1 << bit, index, 0) for bit in range(129) for index in (bit, (bit + 1) % 129)]
    cases += [(rng.getrandbits(129), rng.randrange(160), rng.choice((0, 0, MASK64))) for _ in range(160)]
    rows, expected, held = [], [], 0
    for ordinal, (value, index, high) in enumerate(cases):
        tick, reset, enable = int(ordinal % 4 != 1), int(ordinal % 97 == 0), int(ordinal % 3 != 2)
        flags = [int(index < width and bool(value & (1 << index))) for width in WIDTHS]
        full_index = index | high << 64
        flags += [int(full_index < 129 and bool(value & (1 << full_index))),
                  int((index & 3) < 3 and bool(value & (1 << (index & 3)))), (value >> 128) & 1]
        flags += [int((index & 1) == 0 and bool(value & 1)),
                  int((index & 3) < 3 and bool(value & (1 << (index & 3)))), (value >> (index & 31)) & 1]
        result = sum(flag << bit for bit, flag in enumerate(flags))
        if tick:
            held = 0 if reset else result if enable else held
        # Native input padding must not create extra mask lanes.
        top = (rng.getrandbits(32) & ~1) | (value >> 128)
        rows.append([tick, reset, enable, value & MASK64, (value >> 64) & MASK64, top, index, high])
        expected.append([result, held])
    return '\n'.join(' '.join(f'{x:x}' for x in row) for row in rows) + '\n', expected


def driver(native):
    model = 'rsim_pRsimMaskIndex::Model' if native else 'VRsimMaskIndex'
    header = 'RsimMaskIndex.hpp' if native else 'VRsimMaskIndex.h'
    def port(name):
        return 'dut.inputs.p' + name.replace('_', '_u') if native else 'dut.' + name
    data = port('data') + ('.words' if native else '')
    index = port('wide_index') + ('.words' if native else '')
    outputs = ['dut.outputs().p' + name if native else 'dut.' + name for name in ('result', 'sampled')]
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "{header}"
#include <cstdint>
#include <iostream>
int main() {{
  {model} dut;
  std::uint64_t tick, reset, enable, low, middle, top, lane, high;
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> tick >> reset >> enable >> low >> middle >> top >> lane >> high) {{
    {'' if native else 'dut.clock = 0; dut.eval();'}
    {port('reset')} = reset; {port('enable')} = enable;
    {port('index')} = lane; {port('narrow_index')} = lane & 3;
    {data}[0] = std::uint32_t(low); {data}[1] = std::uint32_t(low >> 32);
    {data}[2] = std::uint32_t(middle); {data}[3] = std::uint32_t(middle >> 32);
    {data}[4] = std::uint32_t(top{'' if native else ' & 1'});
    {index}[0] = std::uint32_t(lane); {index}[1] = std::uint32_t(lane >> 32);
    {index}[2] = std::uint32_t(high); {index}[3] = std::uint32_t(high >> 32);
    dut.eval(); dut.eval();
    if (tick) {{ {'dut.tick();' if native else 'dut.clock = 1; dut.eval();'} }}
    std::cout << std::uint64_t({outputs[0]}) << ' ' << std::uint64_t({outputs[1]}) << '\\n';
  }}
}}
'''


def run_suite(work, run, compare, differential, circt=False):
    emit = [str(ROOT / 'tools/run-racket.sh'), str(HERE / 'emit-mask-index.rhm'), str(work)]
    run(emit + ['rsim'], work, 'mask-index-emit')
    native = work / 'mask-index-native.cpp'
    native.write_text(driver(True))
    run([*shlex.split(os.environ.get('CXX', 'c++')), '-std=c++17', '-O2',
         '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
         str(work / 'RsimMaskIndex.cpp'), str(native), '-o', str(work / 'mask-index')], work, 'mask-index-build')
    vectors, expected = stimuli()
    compare(run([str(work / 'mask-index')], work, 'mask-index-native', vectors), expected, 'mask indexing')
    for backend in (['verilog'] if differential else []) + (['circt'] if circt else []):
        run(emit + [backend], work, 'mask-index-' + backend + '-emit')
        if backend == 'circt':
            tool = os.environ.get('CIRCT_OPT', str(ROOT / '.tools/firtool-1.155.0/bin/circt-opt'))
            sv = run([tool, '--canonicalize', '--cse', '--prettify-verilog',
                      '--lower-seq-to-sv=disable-mem-randomization=true disable-reg-randomization=true',
                      '--export-verilog', str(work / 'RsimMaskIndex.mlir'), '-o', '/dev/null'], work, 'mask-index-circt-lower')
            (work / 'RsimMaskIndex.sv').write_text(sv)
        sv_driver = work / 'mask-index-sv.cpp'
        sv_driver.write_text(driver(False))
        obj = work / ('mask-index-' + backend + '-obj')
        run(['verilator', '--cc', '--exe', '--build', '-j', '2', '--top-module', 'RsimMaskIndex',
             '--Mdir', str(obj), str(work / 'RsimMaskIndex.sv'), str(sv_driver)], work, 'mask-index-' + backend + '-build')
        compare(run([str(obj / 'VRsimMaskIndex')], work, 'mask-index-' + backend, vectors), expected, 'mask indexing ' + backend)
    return len(expected)


if __name__ == '__main__':
    import run as runner
    parser = argparse.ArgumentParser()
    parser.add_argument('--differential', action='store_true')
    parser.add_argument('--circt', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='rhodium-mask-index-'))
    try:
        count = run_suite(work, runner.run, runner.compare, args.differential, args.circt)
        print(f'{count} mask indexing observations passed')
    except BaseException:
        print(f'mask indexing artifacts retained at {work}')
        raise
    else:
        shutil.rmtree(work)
