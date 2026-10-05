# SPDX-License-Identifier: Apache-2.0
"""Run production UART RTL and its unchanged PTY model through both backends."""
import argparse
import os
import shutil
import tempfile
from pathlib import Path
import shlex

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent


def run_suite(work, run, differential):
    work = work / 'uart'
    work.mkdir(exist_ok=True)
    run([str(ROOT / 'tools/run-racket.sh'), str(HERE / 'emit-uart.rhm'), str(work),
         'differential' if differential else 'native'], work, 'emit-uart')
    host = [str(ROOT / 'devices/uart/dpi/uart_dpi.cc'),
            str(ROOT / 'devices/tests/circt/verilog/uart-dpi_dpi.cpp')]
    bench = str(HERE / 'uart.cpp')
    run(shlex.split(os.environ.get('CXX', 'c++')) +
        ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
         '-I' + str(ROOT), '-I' + str(work), str(work / 'RsimUartPair.cpp'),
         bench, *host, '-o', str(work / 'uart-native')], work, 'build-native')
    native = run([str(work / 'uart-native')], work, 'native')
    if native != '7 6 3\n8 5 3\nUART rsim integration passed\n':
        raise AssertionError('unexpected UART completion counts; see native.log')
    if differential:
        run(['verilator', '--cc', '--exe', '--build', '--assert', '-Wno-UNSIGNED',
             '-j', '2', '--top-module', 'RsimUartPair', '--Mdir', str(work / 'obj'),
             '-CFLAGS', '-DRSIM_REFERENCE -I' + shlex.quote(str(ROOT)),
             str(work / 'RsimUartPair.sv'), bench, *host], work, 'build-reference')
        reference = run([str(work / 'obj/VRsimUartPair')], work, 'reference')
        if reference != native:
            raise AssertionError('UART behavioral outcomes disagree between backends')
    return 2


if __name__ == '__main__':
    from run import run
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--differential', action='store_true')
    args = parser.parse_args()
    directory = Path(tempfile.mkdtemp(prefix='rhodium-rsim-uart-'))
    try:
        count = run_suite(directory, run, args.differential)
        print(f'UART/PTY: {count} independent instances passed' + (' on both backends' if args.differential else ' on rsim'))
    except BaseException:
        print(f'UART artifacts and logs retained at {directory}')
        raise
    else:
        shutil.rmtree(directory)
