#!/usr/bin/env python3
# Builds pinned upstream workloads without modifying their source or selecting by pass status.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from program_target import (elf_architecture, instruction_inventory, load_target, objdump_for,
                            probe_compiler, readelf_for, target_fingerprint)

SCALAR_BENCHMARKS = ('median', 'qsort', 'rsort', 'towers', 'vvadd', 'memcpy',
                     'multiply', 'mm', 'dhrystone', 'spmv')
VECTOR_BENCHMARKS = ('vec-memcpy', 'vec-daxpy', 'vec-sgemm', 'vec-strcmp')
MULTIHART_BENCHMARKS = ('mt-vvadd', 'mt-matmul', 'mt-memcpy')
MULTIHART_BENCHMARK_REQUIREMENTS = {'mt-vvadd': frozenset(('d',)),
                                    'mt-matmul': frozenset(),
                                    'mt-memcpy': frozenset()}
FLAGS = ('-U_FORTIFY_SOURCE -DPREALLOCATE=0 -mcmodel=medany -static -std=gnu99 '
         '-O2 -ffast-math -fno-common -fno-builtin-printf '
         '-fno-tree-loop-distribute-patterns -Wno-implicit-int '
         '-Wno-implicit-function-declaration')
BASELINE_MARCH = 'rv64imafdc_zicsr_zifencei'
BASELINE_MABI = 'lp64d'
HERE = Path(__file__).resolve().parent

# Upstream ISA groups supported by the adapter, keyed by the extension that
# makes each group's physical and available virtual tests applicable.
ISA_GROUPS = {
    'i': 'rv64ui',
    'm': 'rv64um',
    'a': 'rv64ua',
    'f': 'rv64uf',
    'd': 'rv64ud',
    'c': 'rv64uc',
    'zba': 'rv64uzba',
    'zbb': 'rv64uzbb',
    'zbs': 'rv64uzbs',
    'zicond': 'rv64uzicond',
    'zicboz': 'rv64mzicbo',
}

# Representative operations, chosen by feature rather than observed pass status.
SMOKE_TESTS = {
    'i': ('add', 'sub', 'sll', 'sltu', 'beq', 'bne', 'jalr', 'lb', 'ld', 'sb', 'sd'),
    'm': ('mul', 'mulh', 'div', 'rem'),
    'a': ('amoadd_d', 'amoswap_w', 'lrsc'),
    'c': ('rvc',),
    'zba': ('sh1add',),
    'zbb': ('clz',),
    'zbs': ('bset',),
    'zicond': ('czero_eqz',),
    'zicboz': ('zero',),
}


def isa_groups(target):
    if target['xlen'] != 64 or 'i' not in target['extensions']:
        raise ValueError('ISA tests require an RV64 I target')
    return [group for extension, group in ISA_GROUPS.items() if extension in target['extensions']]


def virtual_environment_enabled(target):
    return (target.get('mmu_mode') == 'sv39'
            and {'m', 's', 'u'} <= set(target.get('privilege_modes', [])))


def smoke_selection(target):
    isa_groups(target)
    extensions = [extension for extension in SMOKE_TESTS if extension in target['extensions']]
    return ([ISA_GROUPS[extension] for extension in extensions],
            [f'{ISA_GROUPS[extension]}-p-{test}'
             for extension in extensions for test in SMOKE_TESTS[extension]])


def multihart_benchmark_harts(target):
    harts = target['harts']
    if len(harts) < 2 or harts != list(range(len(harts))) or 'a' not in target['extensions']:
        raise ValueError('multihart benchmarks require at least two contiguous harts starting at zero and A')
    return harts


def benchmark_selection(target, mode, selection='single-hart'):
    if selection == 'multihart':
        if mode != 'target':
            raise ValueError('multihart benchmarks require target-native compilation')
        multihart_benchmark_harts(target)
        extensions = set(target['extensions'])
        return tuple(benchmark for benchmark in MULTIHART_BENCHMARKS
                     if MULTIHART_BENCHMARK_REQUIREMENTS[benchmark] <= extensions)
    vector = VECTOR_BENCHMARKS if mode == 'target' and 'v' in target['extensions'] else ()
    return SCALAR_BENCHMARKS + vector


def materialize_multihart_source(source, destination, hart_count, benchmarks):
    source_benchmarks = source / 'benchmarks'
    generated = destination / 'benchmarks'
    if destination.exists():
        shutil.rmtree(destination)
    generated.mkdir(parents=True)
    os.symlink(source / 'env', destination / 'env', target_is_directory=True)
    shutil.copytree(source_benchmarks / 'common', generated / 'common')
    for benchmark in benchmarks:
        os.symlink(source_benchmarks / benchmark, generated / benchmark, target_is_directory=True)
    crt = generated / 'common/crt.S'
    text = crt.read_text()
    marker = '  # for now, assume only 1 core\n  li a1, 1\n'
    if text.count(marker) != 1:
        raise ValueError('upstream multihart runtime core-count marker changed')
    crt.write_text(text.replace(marker,
                                '  # Rhodium build overlay selects the target hart count\n'
                                f'  li a1, {hart_count}\n'))
    syscalls = generated / 'common/syscalls.c'
    source_text = syscalls.read_text()
    exit_marker = 'void exit(int code)\n{\n  tohost_exit(code);\n}\n'
    if source_text.count(exit_marker) != 1:
        raise ValueError('upstream multihart exit runtime changed')
    collective_exit = (f'static volatile int rhodium_exit_count;\n'
                       'static volatile int rhodium_exit_error;\n\n'
                       'void exit(int code)\n{\n'
                       '  if (code)\n'
                       '    __atomic_store_n(&rhodium_exit_error, code, __ATOMIC_RELEASE);\n'
                       '  int completed = __atomic_add_fetch(&rhodium_exit_count, 1, __ATOMIC_ACQ_REL);\n'
                       f'  if (completed == {hart_count})\n'
                       '    tohost_exit(__atomic_load_n(&rhodium_exit_error, __ATOMIC_ACQUIRE));\n'
                       '  while (1);\n}\n')
    syscalls.write_text(source_text.replace(exit_marker, collective_exit))
    return generated


def check_elf_memory(elf, regions, require_executable_entry=True):
    """Check the load footprint, including zero-filled BSS, and entry location.

    These physical ISA assembly tests have no runtime-allocated stack. Any stack
    storage they declare is part of PT_LOAD p_memsz, like their other test data.
    """
    data = elf.read_bytes()
    if len(data) < 64 or data[:7] != b'\x7fELF\x02\x01\x01':
        raise ValueError(f'{elf}: expected a little-endian ELF64 executable')
    header = struct.unpack_from('<HHIQQQIHHHHHH', data, 16)
    kind, machine, _, entry, phoff, _, _, _, phsize, phnum, *_ = header
    if kind != 2 or machine != 243 or phsize != 56 or phoff + phsize * phnum > len(data):
        raise ValueError(f'{elf}: invalid RISC-V executable headers')
    load_entry = False
    executable_entry = False
    footprint = []
    for index in range(phnum):
        ptype, flags, offset, virtual, physical, filesz, memsz, _ = struct.unpack_from('<IIQQQQQQ', data, phoff + index * phsize)
        if ptype != 1:
            continue
        if filesz > memsz or offset + filesz > len(data) or physical != virtual:
            raise ValueError(f'{elf}: invalid physical test load segment')
        if not memsz:
            continue
        if not any(region['base'] <= physical and physical + memsz <= region['base'] + region['size'] for region in regions):
            raise ValueError(f'{elf}: load segment {physical:#x}..{physical + memsz:#x} exceeds target RAM')
        load_entry |= virtual <= entry < virtual + memsz
        executable_entry |= bool(flags & 1) and virtual <= entry < virtual + memsz
        footprint.append(dict(address=physical, memory_bytes=memsz))
    if not load_entry:
        raise ValueError(f'{elf}: entry is not in a RAM load segment')
    if require_executable_entry and not executable_entry:
        raise ValueError(f'{elf}: entry is not in an executable RAM segment')
    return footprint


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=('isa', 'benchmark'), required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--target', type=Path, help='generated concrete SoC program-target descriptor')
    parser.add_argument('--isa-selection', choices=('full', 'smoke'))
    parser.add_argument('--benchmark-mode', choices=('target', 'baseline'), default='target')
    parser.add_argument('--benchmark-selection', choices=('single-hart', 'multihart'), default='single-hart')
    parser.add_argument('--benchmark-hart-count', type=int, choices=(2, 4, 8))
    args = parser.parse_args()
    try:
        target = load_target(args.target) if args.target else None
    except ValueError as error:
        parser.error(str(error))
    if not target:
        parser.error('workload builds require a concrete --target descriptor')
    if args.suite == 'isa' and not args.isa_selection:
        parser.error('ISA builds require --isa-selection=full or --isa-selection=smoke')
    if args.suite != 'isa' and args.isa_selection:
        parser.error('--isa-selection applies only to ISA builds')
    if args.suite != 'benchmark' and args.benchmark_selection != 'single-hart':
        parser.error('--benchmark-selection applies only to benchmark builds')
    if args.benchmark_selection == 'multihart':
        if args.benchmark_hart_count is None:
            parser.error('multihart benchmarks require --benchmark-hart-count=2, 4, or 8')
        if args.benchmark_hart_count > len(multihart_benchmark_harts(target)):
            parser.error('benchmark hart count exceeds the target hart count')
    elif args.benchmark_hart_count is not None:
        parser.error('--benchmark-hart-count applies only to multihart benchmark builds')
    source, output = args.source.resolve(), args.output.resolve()
    compiler = shutil.which(args.compiler)
    if not compiler or not (source / 'env/p/link.ld').is_file():
        parser.error('install the compiler and run make -C sims program-test-setup first')
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    env_revision = subprocess.check_output(['git', '-C', str(source / 'env'), 'rev-parse', 'HEAD'], text=True).strip()
    version = subprocess.check_output([compiler, '--version'], text=True)
    for checkout in (source, source / 'env'):
        subprocess.run(['git', '-C', str(checkout), 'diff', '--quiet', 'HEAD', '--ignore-submodules=untracked'], check=True)
    key = hashlib.sha256((revision + env_revision + version + str(source) + compiler
                          + str(args.isa_selection) + args.benchmark_mode
                          + args.benchmark_selection
                          + str(args.benchmark_hart_count)
                          + json.dumps(target, sort_keys=True)).encode()
                         + Path(__file__).read_bytes() + (HERE / 'program_target.py').read_bytes()
                         + (HERE / 'isa.mk').read_bytes()).hexdigest()
    # Content-addressed directories prevent Make timestamps or restored caches
    # from retaining binaries built with another compiler or selection policy.
    build = output / 'build' / key
    build.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    for generated in ('manifest.json', 'instruction-report.json'):
        (output / generated).unlink(missing_ok=True)
    compiler_arch = None
    if args.suite == 'isa':
        command = ['make', '--no-print-directory', '-s', '-f', str(HERE / 'isa.mk'),
                   'XLEN=64', f'src_dir={source / "isa"}', f'RISCV_GCC={compiler}',
                   'RISCV_GCC_OPTS=-static -mcmodel=medany -fvisibility=hidden -nostdlib -nostartfiles',
                   f'RISCV_PREFIX={compiler.removesuffix("gcc")}']
        groups = isa_groups(target)
        command += ['program_groups=' + ' '.join(groups)]
        virtual = args.isa_selection == 'full' and virtual_environment_enabled(target)
        command += ['program_virtual_groups=' + ' '.join(groups if virtual else [])]
        names = subprocess.check_output(command + ['program-manifest'], cwd=build, text=True).splitlines()
        if args.isa_selection == 'smoke':
            _, selected = smoke_selection(target)
            missing = set(selected) - set(names)
            if missing:
                raise ValueError(f'upstream smoke tests missing from inventory: {sorted(missing)}')
            names = selected
        exclusions = {'rv64ui-p-ma_data': 'Requires successful misaligned data accesses.',
                      'rv64ui-v-ma_data': 'Requires successful misaligned data accesses.',
                      'privileged groups': 'Privileged platform tests are outside this initial ISA adapter.',
                      'other instruction groups': 'Require extensions outside the concrete target profile.'}
        if not virtual:
            exclusions['*-v-*'] = 'Requires the full ISA selection and an Sv39 M/S/U target.'
        if args.isa_selection == 'smoke':
            exclusions['other instruction groups'] = 'Outside the fixed capability-filtered ISA smoke subset.'
    else:
        try:
            benchmarks = benchmark_selection(target, args.benchmark_mode, args.benchmark_selection)
        except ValueError as error:
            parser.error(str(error))
        names = [name + '.riscv' for name in benchmarks]
        if args.benchmark_mode == 'baseline' and target['xlen'] != 64:
            parser.error('the benchmark baseline is defined only for RV64')
        march = target['march'] if args.benchmark_mode == 'target' else BASELINE_MARCH
        mabi = target['mabi'] if args.benchmark_mode == 'target' else BASELINE_MABI
        compiler_arch = probe_compiler(compiler, march, mabi, build)
        benchmark_source = source / 'benchmarks'
        if args.benchmark_selection == 'multihart':
            benchmark_source = materialize_multihart_source(
                source, build / 'multihart-source', args.benchmark_hart_count, benchmarks)
        command = ['make', '--no-print-directory', '-f', str(source / 'benchmarks/Makefile'),
                   f'XLEN={target["xlen"]}', f'src_dir={benchmark_source}', f'RISCV_GCC={compiler}',
                   f'RISCV_MARCH={march}', f'RISCV_VMARCH={march}', f'RISCV_GCC_OPTS={FLAGS} -mabi={mabi}',
                   f'RISCV_LINK_OPTS=-static -nostdlib -nostartfiles -lm -lgcc -T {benchmark_source / "common/test.ld"}']
        if args.benchmark_selection == 'multihart':
            exclusions = {'non-mt benchmarks': 'Outside the focused multihart benchmark selection.',
                          'pmp': 'Requires PMP.'}
            for benchmark in MULTIHART_BENCHMARKS:
                if benchmark not in benchmarks:
                    required = ', '.join(sorted(MULTIHART_BENCHMARK_REQUIREMENTS[benchmark]))
                    exclusions[benchmark] = f'Requires target extensions: {required}.'
        else:
            exclusions = {'mt-*': 'Covered by the tiled SoC multihart benchmark selection.',
                          'pmp': 'Requires PMP.'}
            if args.benchmark_mode != 'target' or 'v' not in target['extensions']:
                exclusions['vec-*'] = 'Requires target-native V compilation.'
    if not names or len(names) != len(set(names)):
        raise RuntimeError('upstream selection is empty or contains duplicate tests')
    stamp = build / 'built.json'
    try:
        cached = json.loads(stamp.read_text())
    except (OSError, ValueError):
        cached = {}
    if not isinstance(cached, dict):
        cached = {}
    reuse = set(cached) == set(names) and all(
        (build / name).is_file() and hashlib.sha256((build / name).read_bytes()).hexdigest() == cached[name]
        for name in names)
    with (output / 'build.log').open('w') as log:
        if reuse:
            log.write(f'Reusing {len(names)} checksum-verified ELFs for {key}\n')
        else:
            # Force rebuilding after a missing or corrupt cached output, even if
            # its timestamp would otherwise satisfy upstream Make's rules.
            result = subprocess.run(command + ['-B'] + names, cwd=build, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError(f'workload build failed; see {output / "build.log"}')
    tests = []
    architecture_report = []
    readelf = readelf_for(compiler) if args.suite == 'benchmark' else None
    objdump = objdump_for(compiler) if args.suite == 'benchmark' else None
    for name in names:
        elf = build / name
        if not elf.is_file():
            raise RuntimeError(f'selected ELF is missing: {elf}')
        tests.append({'name': name, 'elf': str(elf.relative_to(output)),
                      'sha256': hashlib.sha256(elf.read_bytes()).hexdigest()})
        if args.suite == 'benchmark' and args.benchmark_selection == 'multihart':
            tests[-1]['harts'] = target['harts'][:args.benchmark_hart_count]
        if readelf:
            elf_arch = elf_architecture(readelf, elf)
            if elf_arch != compiler_arch:
                raise RuntimeError(f'{elf}: ISA attributes {elf_arch} do not match compiler target {compiler_arch}')
            tests[-1]['elf_arch'] = elf_arch
            architecture_report.append(dict(name=name, elf_arch=elf_arch,
                                            **instruction_inventory(objdump, elf)))
        if target:
            tests[-1]['load_segments'] = check_elf_memory(
                elf, target['ram'], require_executable_entry=args.suite == 'isa')
    manifest = dict(suite=args.suite, revision=revision, env_revision=env_revision,
                    compiler=version, cache_key=key, exclusions=exclusions, tests=tests)
    if target:
        manifest.update(target=target, target_fingerprint=target_fingerprint(target))
    if args.suite == 'isa':
        manifest['selection'] = args.isa_selection
    if args.suite == 'benchmark':
        manifest.update(benchmark_mode=args.benchmark_mode, march=march, mabi=mabi,
                        benchmark_selection=args.benchmark_selection, compiler_arch=compiler_arch)
        if args.benchmark_selection == 'multihart':
            manifest['benchmark_hart_count'] = args.benchmark_hart_count
        (output / 'instruction-report.json').write_text(json.dumps({
            'mode': args.benchmark_mode,
            'march': march,
            'mabi': mabi,
            'compiler_arch': compiler_arch,
            'binaries': architecture_report,
        }, indent=2) + '\n')
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    stamp.write_text(json.dumps({test['name']: test['sha256'] for test in tests}, indent=2) + '\n')
    print(f'Built {len(tests)} {args.suite} workloads; manifest: {output / "manifest.json"}')


if __name__ == '__main__':
    main()
