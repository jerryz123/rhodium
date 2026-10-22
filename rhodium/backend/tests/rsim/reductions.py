# SPDX-License-Identifier: Apache-2.0
"""Check explicit reductions across native, direct SV, and CIRCT lowering."""
import argparse
import os
from pathlib import Path
import random
import re
import shlex
import shutil
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
WIDTHS = (1, 5, 64, 65, 129)


def stimuli():
    rng = random.Random(20261022)
    values = sorted({0, 1, 37, 19, *(1 << i for i in (4, 63, 64, 128)),
                     *((1 << w) - 1 for w in WIDTHS)})
    cases = [(a, b & ((1 << 65) - 1), flags) for a in values for b in values
             for flags in (0, 1, 2, 4, 8, 15)]
    cases += [(rng.getrandbits(129), rng.getrandbits(65), rng.getrandbits(8)) for _ in range(300)]
    rows, expected, held = [], [], 0
    for i, (a, b, flags) in enumerate(cases):
        tick, reset, enable = int(i % 4 != 1), int(i % 97 == 0), int(i % 3 != 0)
        masks = [(1 << w) - 1 for w in WIDTHS]
        terms = [int(bool(a & m)) for m in masks] + [int((a & m) == m) for m in masks]
        terms += [int(bool(a or b)), int(a == (1 << 129) - 1 and b == (1 << 65) - 1),
                  int(bool(flags & 1 or (a * b & ((1 << 64) - 1)) == 37 or flags & 2)),
                  int(bool(flags & 4 and (a + b & ((1 << 64) - 1)) == 19 and flags & 8)), 0, 1]
        result = sum(term << j for j, term in enumerate(terms))
        if tick:
            held = 0 if reset else result if enable else held
        # Exercise ignored padding in the native wide scalar input carrier.
        aw = [(a >> (32 * j)) & 0xffffffff for j in range(5)]
        bw = [(b >> (32 * j)) & 0xffffffff for j in range(3)]
        rows.append([tick, reset, enable, flags, *aw, *bw])
        expected.append([result, held, (a + b) & ((1 << 64) - 1)])
    return '\n'.join(' '.join(f'{v:x}' for v in row) for row in rows) + '\n', expected


def driver(native):
    def inp(name):
        return 'dut.inputs.p' + name if native else 'dut.' + name
    def out(name):
        return 'dut.outputs().p' + name if native else 'dut.' + name
    probe = '''
  dut.inputs.pflags = 1;
  reduction_products = 0; dut.eval();
  if (reduction_products != 0) return 11;
  dut.inputs.pflags = 0;
  reduction_products = 0; dut.eval();
  if (reduction_products == 0) return 12;
''' if native else ''
    assignments = '\n'.join(
        f'    {inp(name)}{suffix}[{j}] = words[{offset+j}]' +
        (' | 0xfffffffeU' if native and j == size-1 else '') + ';'
        for name, size, offset in [('data', 5, 0), ('other', 3, 5)]
        for j in range(size) for suffix in ['.words' if native else ''])
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "{'RsimReductions.hpp' if native else 'VRsimReductions.h'}"
#include <cstdint>
#include <iostream>
{'extern std::uint64_t reduction_products;' if native else ''}
int main() {{
  {'rsim_pRsimReductions::Model' if native else 'VRsimReductions'} dut;
{probe}
  std::uint64_t tick, reset, enable, flags; std::uint32_t words[8];
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> tick >> reset >> enable >> flags) {{
    for (auto& word : words) std::cin >> word;
    {'' if native else 'dut.clock = 0; dut.eval();'}
    {inp('reset')} = reset; {inp('enable')} = enable; {inp('flags')} = flags;
{assignments}
    dut.eval(); dut.eval();
    if (tick) {{ {'dut.tick();' if native else 'dut.clock = 1; dut.eval();'} }}
    std::cout << std::uint64_t({out('result')}) << ' ' << std::uint64_t({out('sampled')}) << ' ' << std::uint64_t({out('shared')}) << '\\n';
  }}
}}
'''


def run_suite(work, run, compare, differential, circt=False):
    emit = [str(ROOT / 'tools/run-racket.sh'), str(HERE / 'emit-reductions.rhm'), str(work)]
    run(emit + ['rsim'], work, 'reductions-emit')
    source = work / 'RsimReductions.cpp'
    text = source.read_text()
    # Count actual native multiplication executions, including inside region helpers.
    text, count = re.subn(r'(^\s*)(\[\[maybe_unused\]\] const std::uint64_t v\d+ = [^;\n]* \* [^;\n]*;)',
                         r'\1++reduction_products;\1\2', text, flags=re.M)
    if count == 0:
        raise AssertionError('missing multiplication execution probe')
    source.write_text('#include <cstdint>\nstd::uint64_t reduction_products = 0;\n' + text)
    main = work / 'reductions-main.cpp'
    main.write_text(driver(True))
    run([*shlex.split(os.environ.get('CXX', 'c++')), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(source), str(main),
         '-o', str(work / 'reductions')], work, 'reductions-build')
    vectors, expected = stimuli()
    compare(run([str(work / 'reductions')], work, 'reductions-native', vectors), expected, 'reductions')
    for backend in (['verilog'] if differential else []) + (['circt'] if circt else []):
        run(emit + [backend], work, 'reductions-' + backend + '-emit')
        if backend == 'circt':
            tool = os.environ.get('CIRCT_OPT', str(ROOT / '.tools/firtool-1.155.0/bin/circt-opt'))
            sv = run([tool, '--canonicalize', '--cse', '--prettify-verilog',
                      '--lower-seq-to-sv=disable-mem-randomization=true disable-reg-randomization=true',
                      '--export-verilog', str(work / 'RsimReductions.mlir'), '-o', '/dev/null'], work, 'reductions-circt-lower')
            (work / 'RsimReductions.sv').write_text(sv)
        main.write_text(driver(False))
        obj = work / ('reductions-' + backend + '-obj')
        run(['verilator', '--cc', '--exe', '--build', '-j', '2', '--top-module', 'RsimReductions',
             '--Mdir', str(obj), str(work / 'RsimReductions.sv'), str(main)], work, 'reductions-' + backend + '-build')
        compare(run([str(obj / 'VRsimReductions')], work, 'reductions-' + backend, vectors), expected, 'reductions ' + backend)
    return len(expected)


if __name__ == '__main__':
    import run as runner
    parser = argparse.ArgumentParser()
    parser.add_argument('--differential', action='store_true')
    parser.add_argument('--circt', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='rhodium-reductions-'))
    try:
        count = run_suite(work, runner.run, runner.compare, args.differential, args.circt)
        print(f'{count} reduction observations and conditional execution probes passed')
    except BaseException:
        print(f'reduction artifacts retained at {work}')
        raise
    else:
        shutil.rmtree(work)
