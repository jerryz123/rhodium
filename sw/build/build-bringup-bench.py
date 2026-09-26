#!/usr/bin/env python3
# Builds the bounded Bringup-Bench inventory against the selected SoC's RV64 target.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from build import check_elf_memory
from program_target import (elf_architecture, instruction_inventory, load_target, objdump_for,
                            probe_compiler, readelf_for, target_fingerprint)
from riscv.patched_submodule import materialize, read_series

COMMON_FLAGS = ('-O3', '-std=gnu99', '-mcmodel=medany', '-static', '-ffreestanding',
                '-fno-common', '-fno-builtin', '-fno-pie', '-fno-tree-loop-distribute-patterns',
                '-DLIBMIN_MALLOC_ALIGN_BYTES=8', '-DTARGET_RHODIUM')
HASH_PATTERN = re.compile(r'\*\* hashval = 0x([0-9a-fA-F]{16})\n?\Z')
BOUNDED_HASHES = {
    'ackermann': 0x81b399a6d67918f5,
    'anagram': 0x5f33f1cf1e089aa6,
    'checkers': 0x37ac1dba0e7ce03d,
    'connect4-minimax': 0x9c48901ef06d6c1e,
    'donut': 0xf16700b00ab6f89d,
    'edit-distance': 0xabdc5549cb129961,
    'highlife': 0x6724467d3e0f415a,
    'idct-alg': 0x74e0c727d02b5582,
    'life': 0xd147933b9458b72c,
    'lz-compress': 0x53fca8bc9964bc45,
    'matmult': 0xe60b92a2459ad019,
    'monte-carlo': 0xa80f3b7e3e9bd479,
    'n-queens': 0x233a3c3760158a33,
    'nbody-sim': 0x92805a97dac4a1ac,
    'nonlinear-nn': 0xbe603a0aa04606c6,
    'parrondo': 0x70289d3bd962fb6b,
    'pi-calc': 0x0edc910867a78daa,
    'rand-test': 0x77cae076ea39c4a5,
    'ransac': 0xa8ad3154f2582ca4,
    'rho-factor': 0x041d0ed8c5b81da9,
    'sudoku-solver': 0x90cc4fb99821d551,
    'tetris-sim': 0x394af9ded5713658,
}


def upstream_inventory(source):
    makefile = (source / 'Makefile').read_text()
    match = re.search(r'^BMARKS = (.+)$', makefile, re.MULTILINE)
    if not match:
        raise ValueError('Bringup-Bench Makefile has no BMARKS inventory')
    names = match.group(1).split()
    if not names or len(names) != len(set(names)) or any(not re.fullmatch(r'[a-zA-Z0-9-]+', n) for n in names):
        raise ValueError('Bringup-Bench BMARKS inventory is empty, duplicated, or unsafe')
    common_block = makefile.split('__LIBMIN_SRCS =', 1)[1].split('LIBMIN_SRCS =', 1)[0]
    common_names = re.findall(r'libmin_[a-z0-9_]+\.c', common_block)
    actual_common = {path.name for path in (source / 'common').glob('libmin_*.c')}
    if not common_names or set(common_names) != actual_common or len(common_names) != len(actual_common):
        raise ValueError('Bringup-Bench common source inventory changed')
    benchmarks = {}
    for name in names:
        directory = source / name
        local = (directory / 'Makefile').read_text()
        objects = re.findall(r'^LOCAL_OBJS=(.*)$', local, re.MULTILINE)
        flags = re.findall(r'^LOCAL_CFLAGS=(.*)$', local, re.MULTILINE)
        libraries = re.findall(r'^LOCAL_LIBS=(.*)$', local, re.MULTILINE)
        programs = re.findall(r'^PROG=(.*)$', local, re.MULTILINE)
        if (len(objects) != 1 or len(flags) != 1 or len(libraries) != 1 or len(programs) != 1
                or flags[0].strip() or libraries[0].strip() or programs[0].strip() != name):
            raise ValueError(f'{name}: unsupported upstream build recipe')
        object_names = objects[0].split()
        if not object_names or any(not re.fullmatch(r'[a-zA-Z0-9_-]+\.o', obj) for obj in object_names):
            raise ValueError(f'{name}: invalid source inventory')
        sources = [directory / (obj[:-2] + '.c') for obj in object_names]
        if any(not path.is_file() for path in sources):
            raise ValueError(f'{name}: upstream source is missing')
        hash_text = (directory / (name + '.hash')).read_text()
        hash_match = HASH_PATTERN.fullmatch(hash_text)
        if not hash_match:
            raise ValueError(f'{name}: invalid upstream output hash')
        benchmarks[name] = dict(sources=sources, expected_hash=int(hash_match.group(1), 16))
    return [source / 'common' / name for name in common_names], benchmarks


def verify_upstream(source):
    common, benchmarks = upstream_inventory(source)
    required = [source / 'Makefile', source / 'common/libmin.h', source / 'target/libtarg.h',
                *common]
    for name, info in benchmarks.items():
        required.extend((source / name / 'Makefile', source / name / (name + '.hash')))
        required.extend(info['sources'])
    relative = [str(path.relative_to(source)) for path in required]
    subprocess.run(['git', '-C', str(source), 'ls-files', '--error-unmatch', *relative],
                   check=True, stdout=subprocess.DEVNULL)
    if subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True):
        raise ValueError('Bringup-Bench submodule must be pristine')
    return common, benchmarks


def rhodium_header(text):
    marker = 'defined(TARGET_HASPIKE) || defined(TARGET_CVA6_RV64)'
    if text.count(marker) != 1:
        raise ValueError('Bringup-Bench target header changed; update the Rhodium adapter')
    return text.replace(marker, marker + ' || defined(TARGET_RHODIUM)')


def selected_names(inventory, benchmarks=None):
    if not set(BOUNDED_HASHES) <= set(inventory):
        raise ValueError('Bringup-Bench bounded workloads are missing from upstream inventory')
    names = list(inventory)
    if benchmarks:
        selected = benchmarks.split(',')
        if len(set(selected)) != len(selected) or set(selected) - set(names):
            raise ValueError('focused benchmark selection has unknown or duplicate names')
        names = [name for name in names if name in selected]
    return names


def expected_hash(inventory, name):
    return BOUNDED_HASHES.get(name, inventory[name]['expected_hash'])


def run(command, log):
    log.write(' '.join(map(str, command)) + '\n')
    log.flush()
    return subprocess.run(command, stdout=log, stderr=subprocess.STDOUT).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--patch-series', type=Path, required=True)
    parser.add_argument('--port', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--benchmarks', help='Comma-separated focused selection from the upstream inventory')
    args = parser.parse_args()
    source, port, output = args.source.resolve(), args.port.resolve(), args.output.resolve()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error(f'compiler not found: {args.compiler}')
    try:
        target = load_target(args.target)
        _, inventory = verify_upstream(source)
        series, patches = read_series(args.patch_series)
    except (OSError, ValueError, IndexError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    if target['xlen'] != 64:
        parser.error('the Bringup-Bench bare-metal port requires RV64')
    try:
        names = selected_names(inventory, args.benchmarks)
    except ValueError as error:
        parser.error(str(error))
    port_inputs = [port / name for name in ('start.S', 'link.ld.in', 'libtarg.c')]
    if any(not path.is_file() for path in port_inputs):
        parser.error('Bringup-Bench port is incomplete')
    version = subprocess.check_output([compiler, '--version'], text=True)
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    key_input = (revision + version + str(source) + compiler + json.dumps(target, sort_keys=True)
                 + json.dumps(names)).encode()
    for path in [Path(__file__), Path(__file__).with_name('program_target.py'),
                 Path(__file__).resolve().parents[2] / 'riscv/patched_submodule.py',
                 series, *patches, *port_inputs]:
        key_input += path.read_bytes()
    key = hashlib.sha256(key_input).hexdigest()
    build = output / 'build' / key
    build.mkdir(parents=True, exist_ok=True)
    (output / 'manifest.json').unlink(missing_ok=True)
    (output / 'instruction-report.json').unlink(missing_ok=True)
    linker = build / 'link.ld'
    linker.write_text((port / 'link.ld.in').read_text()
                      .replace('@RAM_ORIGIN@', hex(target['ram'][0]['base']))
                      .replace('@RAM_LENGTH@', hex(target['ram'][0]['size'])))
    (build / 'libtarg.h').write_text(rhodium_header((source / 'target/libtarg.h').read_text()))
    compiler_arch = probe_compiler(compiler, target['march'], target['mabi'], build)
    common_flags = [compiler, *COMMON_FLAGS, f'-march={target["march"]}',
                    f'-mabi={target["mabi"]}', f'-I{build}']
    stamp = build / 'built.json'
    try:
        cached = json.loads(stamp.read_text())
    except (OSError, ValueError):
        cached = {}
    filenames = [name + '.riscv' for name in names]
    reuse = isinstance(cached, dict) and set(cached) == set(filenames) and all(
        (build / filename).is_file()
        and hashlib.sha256((build / filename).read_bytes()).hexdigest() == cached[filename]
        for filename in filenames)
    with (output / 'build.log').open('w') as log:
        if reuse:
            log.write(f'Reusing {len(names)} checksum-verified ELFs for {key}\n')
        else:
            patched_source = materialize(source, series, build / 'source')
            common, patched_inventory = upstream_inventory(patched_source)
            if (list(patched_inventory) != list(inventory)
                    or any(patched_inventory[name]['expected_hash'] != inventory[name]['expected_hash']
                           for name in inventory)):
                raise RuntimeError('Bringup-Bench patch series changed benchmark inventory or output oracle')
            compile_flags = [*common_flags, f'-I{patched_source / "common"}']
            workload_flags = [*compile_flags, '-DBRINGUP_BOUNDED']
            common_objects = []
            for path in common:
                obj = build / (path.stem + '.o')
                if run([*compile_flags, '-c', str(path), '-o', str(obj)], log):
                    raise RuntimeError(f'Bringup-Bench common source build failed; see {output / "build.log"}')
                common_objects.append(obj)
            start = build / 'start.o'
            if run([*compile_flags, '-c', str(port / 'start.S'), '-o', str(start)], log):
                raise RuntimeError(f'Bringup-Bench startup build failed; see {output / "build.log"}')
            failures = []
            for name in names:
                work = build / name
                work.mkdir(exist_ok=True)
                objects = []
                for path in patched_inventory[name]['sources']:
                    obj = work / (path.stem + '.o')
                    if run([*workload_flags, f'-I{path.parent}', '-c', str(path), '-o', str(obj)], log):
                        failures.append(name)
                        break
                    objects.append(obj)
                if name in failures:
                    continue
                adapter = work / 'libtarg.o'
                expected = expected_hash(inventory, name)
                if run([*compile_flags, f'-DBRINGUP_EXPECTED_HASH=0x{expected:016x}ULL',
                        '-c', str(port / 'libtarg.c'), '-o', str(adapter)], log):
                    failures.append(name)
                    continue
                elf = build / (name + '.riscv')
                if run([*compile_flags, str(start), *objects, str(adapter), *map(str, common_objects),
                        '-nostdlib', '-nostartfiles', '-no-pie', f'-Wl,-T,{linker}',
                        '-Wl,--gc-sections', '-lgcc', '-o', str(elf)], log):
                    failures.append(name)
            if failures:
                raise RuntimeError(f'Bringup-Bench builds failed: {failures}; see {output / "build.log"}')
    readelf, objdump = readelf_for(compiler), objdump_for(compiler)
    tests, report = [], []
    for name in names:
        elf = build / (name + '.riscv')
        elf_arch = elf_architecture(readelf, elf)
        if elf_arch != compiler_arch:
            raise RuntimeError(f'{name}: ISA attributes {elf_arch} do not match {compiler_arch}')
        tests.append(dict(name=name, elf=str(elf.relative_to(output)),
                          sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), elf_arch=elf_arch,
                          expected_hash=f'0x{expected_hash(inventory, name):016x}',
                          load_segments=check_elf_memory(elf, target['ram'])))
        report.append(dict(name=name, elf_arch=elf_arch, **instruction_inventory(objdump, elf)))
    manifest = dict(suite='bringup-bench', revision=revision, compiler=version, cache_key=key,
                    compiler_flags=' '.join((*COMMON_FLAGS, f'-march={target["march"]}',
                                             f'-mabi={target["mabi"]}')),
                    patch_series_sha256=hashlib.sha256(series.read_bytes()).hexdigest(),
                    patches=[dict(name=patch.name, sha256=hashlib.sha256(patch.read_bytes()).hexdigest())
                             for patch in patches],
                    mode='functional', scoring=False,
                    selection='all' if not args.benchmarks else 'focused',
                    upstream_count=len(inventory), march=target['march'], mabi=target['mabi'],
                    compiler_arch=compiler_arch, target=target,
                    target_fingerprint=target_fingerprint(target), tests=tests)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'instruction-report.json').write_text(json.dumps({
        'march': target['march'], 'mabi': target['mabi'], 'compiler_arch': compiler_arch,
        'binaries': report,
    }, indent=2) + '\n')
    stamp.write_text(json.dumps({name + '.riscv': test['sha256'] for name, test in zip(names, tests)},
                                indent=2) + '\n')
    print(f'Built {len(tests)} of {len(inventory)} Bringup-Bench workloads; manifest: {output / "manifest.json"}')


if __name__ == '__main__':
    main()
