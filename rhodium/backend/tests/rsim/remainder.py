# SPDX-License-Identifier: Apache-2.0
"""Compare authored unsigned remainder with Python integers on native, SV, and CIRCT models."""
import argparse
import os
from pathlib import Path
import random
import shlex
import shutil
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
WIDTHS = (1, 5, 32, 64, 65, 129, 512)
OUTPUTS = ('dynamic_result', 'by_three', 'by_one', 'by_power', 'by_limit',
           'by_large', 'guarded', 'widened')


def stimuli(width):
    rng = random.Random(4201 + width)
    mask = (1 << width) - 1
    patterns = {0, 1, mask, mask >> 1, 1 << (width - 1)}
    for bit in (2, 31, 32, 63, 64, 95, 96, 127, 128, 255, 256, 511):
        if bit < width:
            patterns.update((1 << bit, (1 << bit) - 1, (1 << bit) + 1))
    pairs = [(a, b) for a in sorted(patterns) for b in sorted(patterns)]
    if width <= 5:
        pairs = [(a, b) for a in range(mask + 1) for b in range(mask + 1)]
    pairs += [(rng.getrandbits(width), rng.getrandbits(width)) for _ in range(160)]
    rows, expected = [], []
    words = (width + 31) // 32
    for index, (a, b) in enumerate(pairs):
        # Native normalization must remove dirty padding before the division.
        padding = ((1 << (words * 32)) - 1) ^ mask
        raw = (a | (padding if index % 2 else 0), b | (padding if index % 3 else 0))
        rows.append(' '.join(f'{value >> (32 * word) & 0xffffffff:x}'
                             for value in raw for word in range(words)))
        # Observe the raw partial result but never compare it for divisor zero.
        values = [a % b if b else None, a % 3, 0, a % (1 << (width - 1)),
                  a, a, a % b if b else a, ((a + b) & mask) % 3]
        flat = []
        for name, value in zip(OUTPUTS, values):
            bits = width + int(name == 'widened')
            flat.extend(None if value is None else value >> (32 * word) & 0xffffffff
                        for word in range((bits + 31) // 32))
        expected.append(flat)
    return '\n'.join(rows) + '\n', expected


def driver(width, native):
    model = 'rsim_pRemainder::Model' if native else 'VRemainder'
    header = 'Remainder.hpp' if native else 'VRemainder.h'
    reads, writes = [], []
    for name in ('a', 'b'):
        port = 'dut.inputs.p' + name if native else 'dut.' + name
        if width <= 64:
            reads.append(f'{port} = 0;')
            reads.append(f'for (unsigned i = 0; i < {(width+31)//32}; ++i) {{ std::cin >> word; {port} |= std::uint64_t(word) << (32*i); }}')
            if not native and width < 64:
                reads.append(f'{port} &= UINT64_C({(1 << width)-1});')
        else:
            words = port + ('.words' if native else '')
            reads.append(f'for (unsigned i = 0; i < {(width+31)//32}; ++i) {{ std::cin >> word; {words}[i] = word; }}')
            if not native and width % 32:
                reads.append(f'{words}[{width//32}] &= {(1 << (width%32))-1};')
    for name in OUTPUTS:
        bits = width + int(name == 'widened')
        port = 'dut.outputs().p' + name.replace('_', '_u') if native else 'dut.' + name
        if bits > 64:
            port += '.words' if native else ''
            writes.append(f'for (unsigned i = 0; i < {(bits+31)//32}; ++i) {{ std::cout << {port}[i] << " "; }}')
        else:
            writes.append(f'for (unsigned i = 0; i < {(bits+31)//32}; ++i) {{ std::cout << std::uint32_t(std::uint64_t({port}) >> (32*i)) << " "; }}')
    read_lines = '\n    '.join(reads)
    write_lines = '\n    '.join(writes)
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "{header}"
#include <cstdint>
#include <iostream>
int main() {{
  {model} dut;
  std::uint32_t word;
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin.peek() != EOF) {{
    {read_lines}
    if (!std::cin) break;
    dut.eval(); dut.eval();
    {write_lines}
    std::cout << '\\n';
    std::cin >> std::ws;
  }}
}}
'''


def run_suite(work, run, compare, differential, circt=False):
    count = 0
    for width in WIDTHS:
        case = work / f'remainder-{width}'
        case.mkdir()
        emit = [str(ROOT / 'tools/run-racket.sh'), str(HERE / 'emit-remainder.rhm'), str(case)]
        run(emit + ['rsim', str(width)], case, 'emit-native')
        source = case / 'native.cpp'
        source.write_text(driver(width, True))
        binary = case / 'native'
        run([*shlex.split(os.environ.get('CXX', 'c++')), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             str(case / 'Remainder.cpp'), str(source), '-o', str(binary)], case, 'build-native')
        vectors, expected = stimuli(width)
        compare(run([str(binary)], case, 'native', vectors), expected, f'remainder-{width}')
        for backend in (['verilog'] if differential else []) + (['circt'] if circt else []):
            run(emit + [backend, str(width)], case, f'emit-{backend}')
            if backend == 'circt':
                tool = os.environ.get('CIRCT_OPT', str(ROOT / '.tools/firtool-1.155.0/bin/circt-opt'))
                sv = run([tool, '--canonicalize', '--cse', '--prettify-verilog', '--export-verilog',
                          str(case / 'Remainder.mlir'), '-o', '/dev/null'], case, 'lower-circt')
                (case / 'Remainder.sv').write_text(sv)
            source = case / 'reference.cpp'
            source.write_text(driver(width, False))
            obj = case / f'{backend}-obj'
            run(['verilator', '--cc', '--exe', '--build', '-j', '2', '--top-module', 'Remainder',
                 '--Mdir', str(obj), str(case / 'Remainder.sv'), str(source)], case, f'build-{backend}')
            compare(run([str(obj / 'VRemainder')], case, backend, vectors), expected,
                    f'remainder-{width}-{backend}')
        count += len(expected)
    return count


if __name__ == '__main__':
    import run as runner
    parser = argparse.ArgumentParser()
    parser.add_argument('--differential', action='store_true')
    parser.add_argument('--circt', action='store_true')
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='rhodium-remainder-'))
    try:
        count = run_suite(work, runner.run, runner.compare, args.differential, args.circt)
        print(f'{count} remainder observations passed')
    except BaseException:
        print(f'remainder artifacts retained at {work}')
        raise
    else:
        shutil.rmtree(work)
