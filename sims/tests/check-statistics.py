#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'sims/program-test'))
from performance import measure


def run(command, log):
    try:
        result = subprocess.run([str(arg) for arg in command], cwd=ROOT, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=1800)
    except subprocess.TimeoutExpired as error:
        log.write_bytes((error.stdout or b'') + (error.stderr or b''))
        raise RuntimeError(f'{log}: integration command timed out') from error
    log.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f'{log}:\n{result.stdout}\n{result.stderr}')
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--verilator', default='verilator')
    parser.add_argument('--circt-opt', default=ROOT / '.tools/firtool-1.155.0/bin/circt-opt')
    parser.add_argument('--backend', choices=('rsim', 'verilog', 'circt'), action='append')
    args = parser.parse_args()
    for backend in args.backend or ('rsim', 'verilog', 'circt'):
        work = args.build.resolve() / backend
        work.mkdir(parents=True, exist_ok=True)
        options = ['--output-directory', work] if backend == 'rsim' else []
        emitted = run([ROOT / 'tools/run-racket.sh', '-S', ROOT, ROOT / 'sims/emit-soc-harness.rhm',
                       '--backend', backend, *options, '--stats', work,
                       'sims/tests/statistics-harness.rhdl'], work / 'emit.log')
        sv = work / 'SoCHarness.sv'
        if backend == 'circt':
            mlir = work / 'harness.mlir'
            mlir.write_text(emitted)
            emitted = run([args.circt_opt, '--canonicalize', '--cse', '--lower-sim-to-sv',
                           '--lower-seq-to-sv=disable-mem-randomization=true disable-reg-randomization=true',
                           '--export-verilog', mlir, '-o', '/dev/null'], work / 'lower.log')
        sv.write_text(emitted)
        native = [work / 'SoCHarness.cpp', work / 'SoCHarness_bridge.cpp'] if backend == 'rsim' else []
        run([args.verilator, '--binary', '--timing', '--vpi', '--assert', '--build-jobs', '2',
             '--top-module', 'TestDriver', '--Mdir', work / 'obj',
             '-CFLAGS', f'-std=c++20 -DRHODIUM_STATS -I{work}',
             sv, *native, ROOT / 'sims/TestDriver.v', ROOT / 'sims/verilator/simulation_runtime.cc',
             ROOT / 'devices/uart/dpi/uart_dpi.cc',
             ROOT / 'sims/statistics/collector.cc'], work / 'build.log')
        result = run([work / 'obj/VTestDriver', '+max-cycles=5'], work / 'run.log')
        expected = {'cycles': 5, 'rtl/every/events': 5, 'rtl/alternate/events': 2, 'rtl/never': 0}
        assert measure(result, 5) == expected, result
        assert result.count('SoC harness simulation passed') == 1, result
        print(f'{backend}: generated statistics / DPI / TestDriver passed', flush=True)


if __name__ == '__main__':
    main()
