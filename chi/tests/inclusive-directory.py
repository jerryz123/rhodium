#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check directory reset, lane writes, one-cycle reads and fairness against an independent memory oracle."""
import argparse
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(command, work, label, stdin=None):
    result = subprocess.run(command, cwd=ROOT, env=dict(os.environ, PLTCOLLECTS=str(ROOT) + os.pathsep),
                            input=stdin, text=True, capture_output=True, timeout=300)
    (work / (label + '.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f'{label}: {result.stderr[-2500:]}')
    return result.stdout


def cases(slots, set_count):
    rng = random.Random(839 + slots)
    commands = []
    def add(reset=0, lookup=1, address=0, mask=None):
        mask = (1 << slots) - 1 if mask is None else mask
        sets = sum(rng.randrange(set_count) << (2 * i) for i in range(slots))
        ways = rng.getrandbits(slots)
        entries = rng.getrandbits(16 * slots)
        commands.append([reset, lookup, address, mask, sets, ways, entries])
    # Restart during initialization, then hold every client continuously eligible.
    add(reset=1)
    add(); add()
    add(reset=1)
    for _ in range(set_count): add()
    for i in range(60): add(address=i % set_count)
    # Empty classes, changing eligible subsets and repeated writes/reads.
    for i in range(250):
        add(lookup=rng.randrange(2), address=rng.randrange(set_count), mask=rng.randrange(1 << slots))
    # Read every populated row, reset with traffic pending, then prove the sweep
    # erased both ways rather than merely clearing control/response state.
    for address in range(set_count): add(address=address, mask=0)
    add(reset=1)
    for _ in range(set_count): add()
    for address in range(set_count): add(address=address, mask=0)
    add(lookup=0, mask=0)
    memory = [[None, None] for _ in range(set_count)]
    index, initialized, priority, prefer_commit = 0, False, 0, False
    response_valid, response = False, 0
    expected = []
    for reset, lookup, address, mask, sets, ways, entries in commands:
        ready = initialized and not reset
        eligible = [i for i in range(slots) if mask & (1 << i)]
        selected = min(eligible, key=lambda i: (i - priority) % slots) if eligible else None
        write = ready and selected is not None and (not lookup or prefer_commit)
        read = ready and lookup and not write
        valid = ready and response_valid
        expected.append([int(ready), int(read), (1 << selected) if write else 0, int(valid), response if valid else 0])
        if reset:
            index, initialized, priority, prefer_commit = 0, False, 0, False
            response_valid = False
        elif not initialized:
            memory[index] = [0, 0]
            initialized = index == set_count - 1
            index = min(index + 1, set_count - 1)
            response_valid = False
        else:
            response_valid = bool(read)
            if read:
                assert None not in memory[address]
                response = memory[address][0] | memory[address][1] << 16
                prefer_commit = True
            elif write:
                memory[(sets >> (2 * selected)) & 3][(ways >> selected) & 1] = (entries >> (16 * selected)) & 0xffff
                priority = (selected + 1) % slots
                prefer_commit = False
    return '\n'.join(' '.join(format(v, 'x') for v in row) for row in commands) + '\n', expected


def driver(reference):
    include = '#include "VCHIDirectoryFixture.h"' if reference else '#include "CHIDirectoryFixture.hpp"'
    model = 'VCHIDirectoryFixture' if reference else 'rsim_pCHIDirectoryFixture::Model'
    def field(name):
        return 'dut.' + name if reference else 'dut.inputs.p' + name.replace('_', '_u')
    def output(name):
        return 'dut.' + name if reference else 'dut.outputs().p' + name.replace('_', '_u')
    inputs = ['reset', 'lookup_valid', 'lookup_set', 'commit_valid', 'commit_sets', 'commit_ways', 'commit_entries']
    assignments = '\n'.join(f'{field(name)} = a[{i}];' for i, name in enumerate(inputs))
    values = [output(name) for name in ['initialized', 'lookup_accepted', 'commit_accepted', 'response_valid']]
    values.append(f'({output("response_valid")} ? {output("response")} : 0)')
    printed = ' << " " << '.join(f'std::uint64_t({v})' for v in values)
    step = 'dut.clock = 1; dut.eval(); dut.clock = 0; dut.eval();' if reference else 'dut.tick();'
    return f'''// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <iostream>
{include}
int main() {{
  {model} dut;
  std::uint64_t a[7];
  std::cin >> std::hex; std::cout << std::hex;
  {'dut.clock = 0;' if reference else ''}
  while (std::cin >> a[0] >> a[1] >> a[2] >> a[3] >> a[4] >> a[5] >> a[6]) {{
    {assignments}
    dut.eval();
    std::cout << {printed} << '\\n';
    {step}
  }}
}}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path)
    parser.add_argument('--circt-opt', type=Path, help='also compare CIRCT-lowered SV with the same oracle')
    args = parser.parse_args()
    work = (args.work or Path(tempfile.mkdtemp(prefix='chi-directory-'))).resolve()
    work.mkdir(parents=True, exist_ok=True)
    print(f'Directory artifacts: {work}', flush=True)
    for slots, set_count in ((1, 2), (2, 4), (3, 4)):
        case = work / str(slots)
        case.mkdir(exist_ok=True)
        run([str(ROOT/'tools/run-racket.sh'), str(ROOT/'chi/tests/emit-inclusive-directory.rhm'), str(case), str(slots), str(set_count)], case, 'emit')
        vectors, expected = cases(slots, set_count)
        if args.circt_opt:
            circt_sv = run([str(args.circt_opt), '--canonicalize', '--cse', '--lower-seq-firmem', '--lower-sim-to-sv',
                 '--lower-verif-to-sv', '--lower-seq-to-sv=disable-mem-randomization=true disable-reg-randomization=true',
                 '--hw-memory-sim=disable-mem-randomization=true disable-reg-randomization=true read-enable-mode=undefined',
                 '--export-verilog', str(case/'directory.mlir'), '-o', os.devnull], case, 'lower-circt')
            (case/'circt.sv').write_text(circt_sv)
        for label in (['rsim', 'sv', 'circt'] if args.circt_opt else ['rsim', 'sv']):
            reference = label != 'rsim'
            source = case/(label + '.cpp')
            source.write_text(driver(reference))
            if reference:
                run(['verilator', '--cc', '--exe', '--build', '--assert', '-Wno-CMPCONST', '-j', '2', '--top-module', 'CHIDirectoryFixture',
                     '--Mdir', str(case/('obj-' + label)), str(case/('circt.sv' if label == 'circt' else 'CHIDirectoryFixture.sv')), str(source)], case, 'build-' + label)
                binary = case/('obj-' + label)/'VCHIDirectoryFixture'
            else:
                binary = case/'native'
                run(shlex.split(os.environ.get('CXX', 'c++')) + ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(case/'CHIDirectoryFixture.cpp'),
                    str(source), '-o', str(binary)], case, 'build-' + label)
            observed = [[int(v, 16) for v in line.split()] for line in run([str(binary)], case, label, vectors).splitlines()]
            if observed != expected:
                for i, (a, b) in enumerate(zip(observed, expected)):
                    if a != b:
                        raise AssertionError(f'{slots} slots {label} cycle {i}: actual {a}, expected {b}')
                raise AssertionError(f'{slots} slots {label}: output length mismatch')
        print(f'{slots} slots, {set_count} sets: {len(expected)} cycles passed on rsim and direct SV' + (' and CIRCT' if args.circt_opt else ''), flush=True)


if __name__ == '__main__':
    main()
