#!/usr/bin/env python3
# Builds pristine CoreMark sources for the concrete Rhodium RV64 bare-metal target.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build import check_elf_memory
from program_target import (elf_architecture, instruction_inventory, load_target, objdump_for,
                            probe_compiler, readelf_for, target_fingerprint)

CORE_SOURCES = ('core_list_join.c', 'core_main.c', 'core_matrix.c', 'core_state.c', 'core_util.c')
NAME = 'coremark.riscv'
REQUIRED_OUTPUT = ('2K performance run parameters for coremark.',
                   '[0]crclist       : 0xe714',
                   '[0]crcmatrix     : 0x1fd7',
                   '[0]crcstate      : 0x8e3a')
FORBIDDEN_OUTPUT = ('ERROR! list crc', 'ERROR! matrix crc', 'ERROR! state crc',
                    'ERROR! Rhodium CoreMark platform type mismatch', 'Cannot validate operation')
COMMON_FLAGS = ('-O2', '-mcmodel=medany', '-static', '-std=gnu99', '-ffreestanding', '-fno-common',
                '-fno-builtin', '-fno-pie', '-ffunction-sections', '-fdata-sections')


def verify_upstream(source):
    required = CORE_SOURCES + ('coremark.h',)
    if any(not (source / name).is_file() for name in required):
        raise ValueError('CoreMark checkout is missing a required upstream source')
    subprocess.run(['git', '-C', str(source), 'ls-files', '--error-unmatch', *required],
                   check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['git', '-C', str(source), 'diff', '--quiet', 'HEAD'], check=True)


def write_linker(template, destination, region):
    text = template.read_text().replace('@RAM_ORIGIN@', hex(region['base']))
    text = text.replace('@RAM_LENGTH@', hex(region['size']))
    destination.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--port', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--iterations', type=int, required=True)
    args = parser.parse_args()
    if args.iterations <= 0:
        parser.error('iterations must be positive')
    source, port, output = args.source.resolve(), args.port.resolve(), args.output.resolve()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error(f'compiler not found: {args.compiler}')
    try:
        target = load_target(args.target)
        verify_upstream(source)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    if target['xlen'] != 64:
        parser.error('the initial CoreMark bare-metal port requires RV64')
    output.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output([compiler, '--version'], text=True)
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    compiler_arch = probe_compiler(compiler, target['march'], target['mabi'], output)
    port_inputs = [port / name for name in ('core_portme.c', 'core_portme.h', 'htif.c', 'start.S', 'link.ld.in')]
    key_input = (revision + version + str(source) + compiler + str(args.iterations)
                 + json.dumps(target, sort_keys=True)).encode()
    for path in [Path(__file__), Path(__file__).with_name('program_target.py'), *port_inputs]:
        key_input += path.read_bytes()
    key = hashlib.sha256(key_input).hexdigest()
    build = output / 'build' / key
    build.mkdir(parents=True, exist_ok=True)
    for generated in ('manifest.json', 'instruction-report.json'):
        (output / generated).unlink(missing_ok=True)
    linker = build / 'link.ld'
    write_linker(port / 'link.ld.in', linker, target['ram'][0])
    flag_text = ' '.join((*COMMON_FLAGS, f'-march={target["march"]}', f'-mabi={target["mabi"]}'))
    common = [compiler, *COMMON_FLAGS, f'-march={target["march"]}', f'-mabi={target["mabi"]}',
              f'-I{port}', f'-I{source}', f'-DITERATIONS={args.iterations}', '-DTOTAL_DATA_SIZE=2000',
              f'-DRHODIUM_CLOCK_FREQUENCY_HZ={target["clock_frequency_hz"]}',
              f'-DFLAGS_STR="{flag_text}"']
    names = [NAME]
    stamp = build / 'built.json'
    try:
        cached = json.loads(stamp.read_text())
    except (OSError, ValueError):
        cached = {}
    reuse = isinstance(cached, dict) and set(cached) == set(names) and all(
        (build / name).is_file()
        and hashlib.sha256((build / name).read_bytes()).hexdigest() == cached[name]
        for name in names)
    tests = []
    report = []
    readelf = readelf_for(compiler)
    objdump = objdump_for(compiler)
    with (output / 'build.log').open('w') as log:
        elf = build / NAME
        if reuse:
            log.write(f'Reusing checksum-verified ELF {elf}\n')
        else:
            command = [*common, '-DPERFORMANCE_RUN=1', str(port / 'start.S'),
                       *(str(source / name) for name in CORE_SOURCES), str(port / 'core_portme.c'),
                       str(port / 'htif.c'), '-nostdlib', '-nostartfiles', '-no-pie',
                       f'-Wl,-T,{linker}', '-Wl,--gc-sections', '-lgcc', '-o', str(elf)]
            log.write(' '.join(command) + '\n')
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError(f'CoreMark build failed; see {output / "build.log"}')
        elf_arch = elf_architecture(readelf, elf)
        if elf_arch != compiler_arch:
            raise RuntimeError(f'{elf}: ISA attributes {elf_arch} do not match {compiler_arch}')
        test = dict(name=NAME, elf=str(elf.relative_to(output)),
                    sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), elf_arch=elf_arch,
                    load_segments=check_elf_memory(elf, target['ram']),
                    required_output=list(REQUIRED_OUTPUT), forbidden_output=list(FORBIDDEN_OUTPUT))
        tests.append(test)
        report.append(dict(name=NAME, elf_arch=elf_arch, **instruction_inventory(objdump, elf)))
    manifest = dict(suite='coremark', revision=revision, compiler=version, cache_key=key,
                    iterations=args.iterations, march=target['march'], mabi=target['mabi'],
                    compiler_arch=compiler_arch, target=target,
                    target_fingerprint=target_fingerprint(target), tests=tests)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'instruction-report.json').write_text(json.dumps({
        'march': target['march'], 'mabi': target['mabi'], 'compiler_arch': compiler_arch,
        'binaries': report,
    }, indent=2) + '\n')
    stamp.write_text(json.dumps({test['name']: test['sha256'] for test in tests}, indent=2) + '\n')
    print(f'Built CoreMark workload; manifest: {output / "manifest.json"}')


if __name__ == '__main__':
    main()
