#!/usr/bin/env python3
# Builds pinned upstream workloads without modifying their source or selecting by pass status.
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from program_target import (elf_architecture, instruction_inventory, load_target, objdump_for,
                            probe_compiler, readelf_for, target_fingerprint)

BENCHMARKS = ('median', 'qsort', 'rsort', 'towers', 'vvadd', 'memcpy',
              'multiply', 'mm', 'dhrystone', 'spmv')
FLAGS = ('-U_FORTIFY_SOURCE -DPREALLOCATE=0 -mcmodel=medany -static -std=gnu99 '
         '-O2 -ffast-math -fno-common -fno-builtin-printf '
         '-fno-tree-loop-distribute-patterns -Wno-implicit-int '
         '-Wno-implicit-function-declaration')
BASELINE_MARCH = 'rv64imafdc_zicsr_zifencei'
BASELINE_MABI = 'lp64d'
HERE = Path(__file__).resolve().parent

# Representative operations, chosen by feature rather than observed pass status.
SMOKE_GROUPS = {
    'i': ('rv64ui', ('add', 'sub', 'sll', 'sltu', 'beq', 'bne', 'jalr', 'lb', 'ld', 'sb', 'sd')),
    'm': ('rv64um', ('mul', 'mulh', 'div', 'rem')),
    'a': ('rv64ua', ('amoadd_d', 'amoswap_w', 'lrsc')),
    'c': ('rv64uc', ('rvc',)),
    'zba': ('rv64uzba', ('sh1add',)),
    'zbb': ('rv64uzbb', ('clz',)),
    'zbs': ('rv64uzbs', ('bset',)),
    'zicond': ('rv64uzicond', ('czero_eqz',)),
    'zicboz': ('rv64mzicbo', ('zero',)),
}


def smoke_selection(target):
    if target['xlen'] != 64 or 'i' not in target['extensions']:
        raise ValueError('ISA smoke requires an RV64 I target')
    selected = [value for extension, value in SMOKE_GROUPS.items() if extension in target['extensions']]
    return [group for group, _ in selected], [f'{group}-p-{test}' for group, tests in selected for test in tests]


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
    parser.add_argument('--benchmark-mode', choices=('target', 'baseline'), default='target')
    args = parser.parse_args()
    try:
        target = load_target(args.target) if args.target else None
    except ValueError as error:
        parser.error(str(error))
    if args.suite == 'benchmark' and not target:
        parser.error('benchmark builds require a concrete --target descriptor')
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
                          + args.benchmark_mode + json.dumps(target, sort_keys=True)).encode()
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
        if target:
            groups, selected = smoke_selection(target)
            command += ['program_groups=' + ' '.join(groups)]
        names = subprocess.check_output(command + ['program-manifest'], cwd=build, text=True).splitlines()
        if target:
            missing = set(selected) - set(names)
            if missing:
                raise ValueError(f'upstream smoke tests missing from inventory: {sorted(missing)}')
            names = selected
        exclusions = {'rv64ui-p-ma_data': 'Requires successful misaligned data accesses.',
                      '*-v-*': 'Virtual execution environment is outside this initial ISA adapter.',
                      'privileged groups': 'Privileged platform tests are outside this initial ISA adapter.',
                      'other instruction groups': 'Require extensions outside the SimpleSoC instruction profile.'}
        if target:
            exclusions['other instruction groups'] = 'Outside the fixed capability-filtered ISA smoke subset.'
    else:
        names = [name + '.riscv' for name in BENCHMARKS]
        if args.benchmark_mode == 'baseline' and target['xlen'] != 64:
            parser.error('the benchmark baseline is defined only for RV64')
        march = target['march'] if args.benchmark_mode == 'target' else BASELINE_MARCH
        mabi = target['mabi'] if args.benchmark_mode == 'target' else BASELINE_MABI
        compiler_arch = probe_compiler(compiler, march, mabi, build)
        command = ['make', '--no-print-directory', '-f', str(source / 'benchmarks/Makefile'),
                   f'XLEN={target["xlen"]}', f'src_dir={source / "benchmarks"}', f'RISCV_GCC={compiler}',
                   f'RISCV_MARCH={march}', f'RISCV_GCC_OPTS={FLAGS} -mabi={mabi}',
                   f'RISCV_LINK_OPTS=-static -nostdlib -nostartfiles -lm -lgcc -T {source / "benchmarks/common/test.ld"}']
        exclusions = {'mt-*': 'Requires multiple active harts.', 'vec-*': 'Requires V.', 'pmp': 'Requires PMP.'}
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
    if args.suite == 'isa' and target:
        manifest['selection'] = 'smoke'
    if args.suite == 'benchmark':
        manifest.update(benchmark_mode=args.benchmark_mode, march=march, mabi=mabi,
                        compiler_arch=compiler_arch)
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
