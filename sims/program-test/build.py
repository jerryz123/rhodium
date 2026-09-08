#!/usr/bin/env python3
# Builds pinned upstream workloads without modifying their source or selecting by pass status.
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

BENCHMARKS = ('median', 'qsort', 'rsort', 'towers', 'vvadd', 'memcpy',
              'multiply', 'mm', 'dhrystone', 'spmv')
FLAGS = ('-U_FORTIFY_SOURCE -DPREALLOCATE=0 -mcmodel=medany -static -std=gnu99 '
         '-O2 -ffast-math -fno-common -fno-builtin-printf '
         '-fno-tree-loop-distribute-patterns -Wno-implicit-int '
         '-Wno-implicit-function-declaration -mabi=lp64d')
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


def check_elf_memory(elf, regions):
    """Check the load footprint, including zero-filled BSS, and executable entry.

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
        executable_entry |= bool(flags & 1) and virtual <= entry < virtual + memsz
        footprint.append(dict(address=physical, memory_bytes=memsz))
    if not executable_entry:
        raise ValueError(f'{elf}: entry is not in an executable RAM segment')
    return footprint


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=('isa', 'benchmark'), required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--target', type=Path, help='generated SoC description for the ISA smoke subset')
    args = parser.parse_args()
    target = json.loads(args.target.read_text()) if args.target else None
    if target and args.suite != 'isa':
        parser.error('target-specific smoke supports the ISA suite only')
    source, output = args.source.resolve(), args.output.resolve()
    compiler = shutil.which(args.compiler)
    if not compiler or not (source / 'env/p/link.ld').is_file():
        parser.error('install the compiler and run make -C sims program-test-setup first')
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    env_revision = subprocess.check_output(['git', '-C', str(source / 'env'), 'rev-parse', 'HEAD'], text=True).strip()
    version = subprocess.check_output([compiler, '--version'], text=True)
    for checkout in (source, source / 'env'):
        subprocess.run(['git', '-C', str(checkout), 'diff', '--quiet', 'HEAD', '--ignore-submodules=untracked'], check=True)
    key = hashlib.sha256((revision + env_revision + version + str(source) + compiler + json.dumps(target, sort_keys=True)).encode()
                         + Path(__file__).read_bytes() + (HERE / 'isa.mk').read_bytes()).hexdigest()
    # Content-addressed directories prevent Make timestamps or restored caches
    # from retaining binaries built with another compiler or selection policy.
    build = output / 'build' / key
    build.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'manifest.json').unlink(missing_ok=True)
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
        command = ['make', '--no-print-directory', '-f', str(source / 'benchmarks/Makefile'),
                   'XLEN=64', f'src_dir={source / "benchmarks"}', f'RISCV_GCC={compiler}',
                   'RISCV_MARCH=rv64imafdc_zicsr_zifencei', f'RISCV_GCC_OPTS={FLAGS}',
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
    for name in names:
        elf = build / name
        if not elf.is_file():
            raise RuntimeError(f'selected ELF is missing: {elf}')
        tests.append({'name': name, 'elf': str(elf.relative_to(output)),
                      'sha256': hashlib.sha256(elf.read_bytes()).hexdigest()})
        if target:
            tests[-1]['load_segments'] = check_elf_memory(elf, target['ram'])
    manifest = dict(suite=args.suite, revision=revision, env_revision=env_revision,
                    compiler=version, cache_key=key, exclusions=exclusions, tests=tests)
    if target:
        manifest.update(target=target, selection='smoke')
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    stamp.write_text(json.dumps({test['name']: test['sha256'] for test in tests}, indent=2) + '\n')
    print(f'Built {len(tests)} {args.suite} workloads; manifest: {output / "manifest.json"}')


if __name__ == '__main__':
    main()
