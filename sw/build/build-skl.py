#!/usr/bin/env python3
# Builds the upstream capability-selected SKL vector suites with the RV64 bare-metal port.
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
from program_target import (elf_architecture, elf_build_metadata, instruction_inventory, load_target,
                            objdump_for, probe_compiler, readelf_for, target_fingerprint)

from riscv.patched_submodule import materialize, read_series

FLAGS = ('-O3', '-mcmodel=medany', '-ffunction-sections', '-fdata-sections', '-fno-common', '-fno-pie')
PORT_FILES = ('CMakeLists.txt', 'start.S', 'runtime.c', 'link.ld.in')
FUNCTIONAL_PROFILE = dict(depthwise_channels=17, depthwise_spatial_divisor=2,
                          unary_elements=257, gemm_benchmark=[7, 17, 19],
                          gemm_workload_mk_limit=11, softmax_rows=[1, 2, 3, 5],
                          softmax_f32_columns=[1, 27, 67, 60, 28, 64],
                          pack_large_shape=[172, 40], pack_rows=16, transpose_row_modulus=16,
                          gemm_f64_column_limit=37,
                          data_movement_initialization='index-xorshift-full-source-write-only-destination',
                          floating_point_initialization='index-mix32-fp32-fp64-interpolation-rne-bf16',
                          depthwise_output_initialization='allocate-write-only',
                          reporting='integer-counters')


def cached_outputs(entries, cached):
    """Require a complete inventory and matching content for every cached ELF."""
    return isinstance(cached, dict) and set(cached) == {name for name, _ in entries} and all(
        elf.is_file() and hashlib.sha256(elf.read_bytes()).hexdigest() == cached[name]
        for name, elf in entries)


def validate_skl_target(target):
    required = {'i', 'm', 'a', 'f', 'd', 'v', 'zvfh', 'zve32f', 'zve64d'}
    if target['xlen'] != 64 or not required <= set(target['extensions']):
        raise ValueError('SKL requires the RV64 vector/FP capabilities of the RVA23 integration profile')


def verify_upstream(source):
    required = ('CMakeLists.txt', 'test/CMakeLists.txt', 'test/skl-test-driver.c', 'LICENSE.txt')
    if any(not (source / name).is_file() for name in required):
        raise ValueError('SKL checkout is missing a required upstream source')
    if subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True).strip():
        raise ValueError('SKL upstream checkout must remain pristine')
    return subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()


def read_inventory(path, build):
    entries = []
    for line in path.read_text().splitlines():
        fields = line.split('\t')
        if len(fields) != 2 or not re.fullmatch(r'skl_[a-z0-9_]+', fields[0]):
            raise ValueError('invalid SKL CMake inventory entry')
        name, filename = fields
        elf = Path(filename).resolve()
        if name.endswith(('_x390', '_ref')) or not elf.is_relative_to(build.resolve()):
            raise ValueError('SKL inventory violates vector-suite selection or build containment')
        entries.append((name, elf))
    if not entries or [name for name, _ in entries] != sorted({name for name, _ in entries}):
        raise ValueError('SKL inventory must be nonempty, sorted, and unique')
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--patch-series', type=Path, required=True)
    parser.add_argument('--port', type=Path, required=True)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    source, port, output = args.source.resolve(), args.port.resolve(), args.output.resolve()
    compiler, cmake = shutil.which(args.compiler), shutil.which('cmake')
    if not compiler or not cmake or args.jobs < 1:
        parser.error('SKL requires a GNU cross compiler, CMake, and a positive job count')
    try:
        target = load_target(args.target)
        validate_skl_target(target)
        revision = verify_upstream(source)
        series, patches = read_series(args.patch_series)
        port_inputs = [port / name for name in PORT_FILES]
        if any(not path.is_file() for path in port_inputs):
            raise ValueError('SKL bare-metal port is incomplete')
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    version = subprocess.check_output([compiler, '--version'], text=True)
    cmake_version = subprocess.check_output([cmake, '--version'], text=True)
    metadata = elf_build_metadata(target, 'skl', FUNCTIONAL_PROFILE)
    flags = (*FLAGS, f'-march={target["march"]}', f'-mabi={target["mabi"]}')
    key_input = json.dumps(dict(revision=revision, source=str(source), compiler=compiler,
                               compiler_version=version, cmake_version=cmake_version,
                               flags=flags, build_spec=metadata['build_spec']), sort_keys=True).encode()
    for path in [Path(__file__), Path(__file__).with_name('program_target.py'),
                 Path(__file__).with_name('build.py'),
                 Path(__file__).resolve().parents[2] / 'riscv/patched_submodule.py',
                 series, *patches, *port_inputs]:
        key_input += path.read_bytes()
    key = hashlib.sha256(key_input).hexdigest()
    build = output / 'build' / key
    build.mkdir(parents=True, exist_ok=True)
    local_source = build / 'source'
    for name in ('manifest.json', 'instruction-report.json'):
        (output / name).unlink(missing_ok=True)
    linker = build / 'link.ld'
    linker.write_text((port / 'link.ld.in').read_text()
                      .replace('@RAM_ORIGIN@', hex(target['ram'][0]['base']))
                      .replace('@RAM_LENGTH@', hex(target['ram'][0]['size'])))
    stamp = build / 'built.json'
    try:
        entries = read_inventory(build / 'inventory.tsv', build)
        cached = json.loads(stamp.read_text())
    except (OSError, ValueError):
        entries, cached = [], {}
    with (output / 'build.log').open('w') as log:
        if entries and cached_outputs(entries, cached):
            log.write(f'Reusing {len(entries)} checksum-verified SKL ELFs for {key}\n')
        else:
            materialize(source, series, local_source)
            command = [cmake, '-S', str(port), '-B', str(build), '-DCMAKE_SYSTEM_NAME=Generic',
                       '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY', f'-DCMAKE_C_COMPILER={compiler}',
                       f'-DCMAKE_ASM_COMPILER={compiler}', '-DCMAKE_BUILD_TYPE=Release',
                       f'-DCMAKE_C_FLAGS={" ".join(flags)}', f'-DCMAKE_ASM_FLAGS={" ".join(flags)}',
                       f'-DSKL_SOURCE={local_source}', f'-DSKL_LINKER={linker}']
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
            entries = read_inventory(build / 'inventory.tsv', build)
            cached = cached if isinstance(cached, dict) else {}
            # A corrupt output can otherwise look newer than every CMake input.
            for name, elf in entries:
                if elf.is_file() and hashlib.sha256(elf.read_bytes()).hexdigest() != cached.get(name):
                    elf.unlink()
            subprocess.run([cmake, '--build', str(build), '--parallel', str(args.jobs)],
                           check=True, stdout=log, stderr=subprocess.STDOUT)
    compiler_arch = probe_compiler(compiler, target['march'], target['mabi'], build)
    readelf, objdump = readelf_for(compiler), objdump_for(compiler)
    tests, report = [], []
    for name, elf in entries:
        arch = elf_architecture(readelf, elf)
        if arch != compiler_arch:
            raise RuntimeError(f'{elf}: ISA attributes {arch} do not match {compiler_arch}')
        tests.append(dict(name=name, elf=str(elf.relative_to(output)),
                          sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), elf_arch=arch,
                          load_segments=check_elf_memory(elf, target['ram']),
                          required_output=[f'SKL-RESULT: {name} PASS'],
                          forbidden_output=['SKL trap', 'Test failed']))
        report.append(dict(name=name, elf_arch=arch, **instruction_inventory(objdump, elf)))
    files = []
    for name, path in [('LICENSE.skl', source / 'LICENSE.txt'),
                       ('LICENSE.rhodium', Path(__file__).resolve().parents[2] / 'LICENSE')]:
        data = path.read_bytes()
        (output / name).write_bytes(data)
        files.append(dict(path=name, sha256=hashlib.sha256(data).hexdigest()))
    manifest = dict(suite='skl', revision=revision, compiler=version, cmake=cmake_version,
                    patch_series_sha256=hashlib.sha256(series.read_bytes()).hexdigest(),
                    patches=[dict(name=patch.name, sha256=hashlib.sha256(patch.read_bytes()).hexdigest())
                             for patch in patches],
                    compiler_flags=list(flags), compiler_arch=compiler_arch, cache_key=key,
                    mode='execution-only', scoring=False,
                    march=target['march'], mabi=target['mabi'], target=target,
                    target_fingerprint=target_fingerprint(target), tests=tests, files=files, **metadata)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'instruction-report.json').write_text(json.dumps(dict(binaries=report), indent=2) + '\n')
    stamp.write_text(json.dumps({test['name']: test['sha256'] for test in tests}, indent=2) + '\n')
    print(f'Built {len(tests)} SKL vector suites; manifest: {output / "manifest.json"}')


if __name__ == '__main__':
    main()
