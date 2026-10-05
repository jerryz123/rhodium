# SPDX-License-Identifier: Apache-2.0
"""Qualify one production CHI DPI memory through SV-hosted rsim and direct SV."""
import os
from pathlib import Path
import shlex
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent


def run_suite(work, run):
    work = work / 'chi-memory'
    work.mkdir()
    run([str(ROOT / 'tools/run-racket.sh'), str(HERE / 'emit-chi-memory.rhm'), str(work)], work, 'emit')
    transcripts = {}
    dpi = ROOT / 'chi/subordinate/dpi'
    for mode in ('rsim', 'reference'):
        obj = work / mode
        sources = [work / (('reference-' if mode == 'reference' else '') + 'RsimCHIMemory.sv'),
                   HERE / 'chi-memory-bench.sv', HERE / 'chi-memory-host.cpp',
                   dpi / 'chi_dpi_memory_dpi.cc', dpi / 'chi_memory.cc']
        if mode == 'rsim':
            sources += [work / 'RsimCHIMemory.cpp', work / 'RsimCHIMemory_bridge.cpp']
        flags = f'-std=c++20 -I{shlex.quote(str(dpi))} -include VCHIMemoryBench__Dpi.h'
        run([os.environ.get('VERILATOR', 'verilator'), '--binary', '--timing', '--assert',
             '-Wno-UNSIGNED', '-Wno-CMPCONST', '-Wno-UNOPTFLAT', '-j', '2',
             '--top-module', 'CHIMemoryBench', '--Mdir', str(obj), '-I' + str(work),
             '-CFLAGS', flags, *map(str, sources)], work, 'build-' + mode)
        output = run([str(obj / 'VCHIMemoryBench')], work, mode)
        if 'CHI_MEMORY_PASS' not in output:
            raise AssertionError(f'{mode}: missing completion marker')
        transcripts[mode] = [line for line in output.splitlines() if line.startswith(('CYCLE ', 'RSP ', 'DAT '))]
    if transcripts['rsim'] != transcripts['reference']:
        for i, (left, right) in enumerate(zip(transcripts['rsim'], transcripts['reference'])):
            if left != right:
                raise AssertionError(f'CHI cycle mismatch at {i}: {left} != {right}')
        raise AssertionError('CHI transcript lengths differ')
    return len(transcripts['rsim'])


if __name__ == '__main__':
    from run import run
    work = Path(tempfile.mkdtemp(prefix='rhodium-rsim-chi-'))
    try:
        count = run_suite(work, run)
        print(f'CHI memory: {count} cycle/response observations passed on both backends')
    except BaseException:
        print(f'CHI memory artifacts and logs retained at {work}')
        raise
    else:
        shutil.rmtree(work)
