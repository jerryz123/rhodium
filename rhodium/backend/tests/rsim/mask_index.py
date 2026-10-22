# SPDX-License-Identifier: Apache-2.0
"""Check mask indexing, index/prefix/range constructors, and capture against integer oracles."""
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
DECODE_BITS = 129 * 11
DECODE_WORDS = (DECODE_BITS + 31) // 32
BOUND_WORDS = (129 * 24 + 31) // 32


def stimuli():
    rng = random.Random(1109)
    indices = list(range(131)) + [255, 1 << 32, 1 << 63, MASK64]
    # Exercise each upper count limb independently, including its high bit.
    high_counts = (0, 1, 1 << 31, 1 << 32, 1 << 63)
    cases = [(value, index, high) for value in (0, (1 << 129) - 1, 1, 1 << 128)
             for index in indices for high in high_counts]
    cases += [(1 << bit, index, 0) for bit in range(129) for index in (bit, (bit + 1) % 129)]
    cases += [(rng.getrandbits(129), rng.randrange(160), rng.choice((0, 0, MASK64))) for _ in range(160)]
    cases = [(value, index, high, rng.choice(indices), rng.choice(high_counts))
             for value, index, high in cases]
    edges = (0, 1, 2, 3, 31, 32, 63, 64, 65, 128, 129, 130, 1 << 32, 1 << 63, MASK64)
    cases += [(0, first, 0, end, 0) for first in edges for end in edges]
    cases += [(0, first, 0, end, 0) for first in range(10) for end in range(10)]
    rows, expected, held = [], [], 0
    for ordinal, (value, index, high, end, end_high) in enumerate(cases):
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
        rows.append([tick, reset, enable, value & MASK64, (value >> 64) & MASK64, top, index, high, end, end_high])
        decoded = [int(bool(enable)) << index if index < width else 0 for width in WIDTHS]
        decoded += [1 << index if index < 129 else 0,
                    int(bool(enable)) << full_index if full_index < 129 else 0,
                    int(bool(enable)) << (index & 3)]
        packed_decoded = sum(mask << (129 * lane) for lane, mask in enumerate(decoded))

        # Membership is the oracle, independent of the constructor's prefix intersection.
        def interval(width, first, limit):
            return sum(1 << lane for lane in range(width) if first <= lane < limit)

        full_end = end | end_high << 64
        bounds = [interval(width, 0, index) for width in WIDTHS]
        bounds += [interval(width, full_index, full_end) for width in WIDTHS]
        bounds += [interval(129, 0, index & 3), interval(129, index & 3, end & 3),
                   interval(129, 1, end), interval(129, full_index, 129),
                   (1 << 129) - 1, 1 << 128,
                   interval(129, index & 3, full_end), interval(129, full_index, end & 3)]
        packed_bounds = sum(mask << (129 * lane) for lane, mask in enumerate(bounds))
        expected.append([result, held] + [(packed_decoded >> (32 * word)) & 0xffffffff
                                          for word in range(DECODE_WORDS)]
                        + [(packed_bounds >> (32 * word)) & 0xffffffff
                           for word in range(BOUND_WORDS)])
    return '\n'.join(' '.join(f'{x:x}' for x in row) for row in rows) + '\n', expected


def driver(native):
    model = 'rsim_pRsimMaskIndex::Model' if native else 'VRsimMaskIndex'
    header = 'RsimMaskIndex.hpp' if native else 'VRsimMaskIndex.h'
    def port(name):
        return 'dut.inputs.p' + name.replace('_', '_u') if native else 'dut.' + name
    data = port('data') + ('.words' if native else '')
    index = port('wide_index') + ('.words' if native else '')
    end = port('limit') + ('.words' if native else '')
    outputs = ['dut.outputs().p' + name if native else 'dut.' + name for name in ('result', 'sampled')]
    decoded = 'dut.outputs().pdecoded.words' if native else 'dut.decoded'
    bounds = 'dut.outputs().pbounds.words' if native else 'dut.bounds'
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "{header}"
#include <cstdint>
#include <iostream>
int main() {{
  {model} dut;
  std::uint64_t tick, reset, enable, low, middle, top, lane, high, end, end_high;
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> tick >> reset >> enable >> low >> middle >> top >> lane >> high >> end >> end_high) {{
    {'' if native else 'dut.clock = 0; dut.eval();'}
    {port('reset')} = reset; {port('enable')} = enable;
    {port('index')} = lane; {port('narrow_index')} = lane & 3;
    {data}[0] = std::uint32_t(low); {data}[1] = std::uint32_t(low >> 32);
    {data}[2] = std::uint32_t(middle); {data}[3] = std::uint32_t(middle >> 32);
    {data}[4] = std::uint32_t(top{'' if native else ' & 1'});
    {index}[0] = std::uint32_t(lane); {index}[1] = std::uint32_t(lane >> 32);
    {index}[2] = std::uint32_t(high); {index}[3] = std::uint32_t(high >> 32);
    {end}[0] = std::uint32_t(end); {end}[1] = std::uint32_t(end >> 32);
    {end}[2] = std::uint32_t(end_high); {end}[3] = std::uint32_t(end_high >> 32);
    dut.eval(); dut.eval();
    if (tick) {{ {'dut.tick();' if native else 'dut.clock = 1; dut.eval();'} }}
    std::cout << std::uint64_t({outputs[0]}) << ' ' << std::uint64_t({outputs[1]});
    for (unsigned word = 0; word < {DECODE_WORDS}; ++word)
      std::cout << ' ' << std::uint64_t({decoded}[word]);
    for (unsigned word = 0; word < {BOUND_WORDS}; ++word)
      std::cout << ' ' << std::uint64_t({bounds}[word]);
    std::cout << '\\n';
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
