#!/usr/bin/env python3
# Builds the current upstream Embench-IoT tree as target-bound functional workloads.
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
from build import check_elf_memory
from program_target import (elf_architecture, instruction_inventory, load_target, objdump_for,
                            probe_compiler, readelf_for, target_fingerprint)

BENCHMARKS = ('aha-mont64', 'crc32', 'depthconv', 'edn', 'huffbench', 'matmult-int',
              'md5sum', 'nettle-aes', 'nettle-sha256', 'nsichneu', 'picojpeg',
              'qrduino', 'sglib-combined', 'slre', 'statemate', 'tarfind', 'ud',
              'wikisort', 'xgboost')
FUNCTIONAL_PROFILES = dict(
    xgboost=dict(sample_indices=(3, 2, 1, 18, 4, 8, 11, 0, 61, 7), expected_correct=9))
SUPPORT_SOURCES = ('support/main.c', 'support/beebsc.c')
COMMON_FLAGS = ('-O3', '-mcmodel=medany', '-static', '-std=gnu99', '-ffreestanding',
                '-fno-common', '-fno-builtin', '-fno-pie', '-ffunction-sections',
                '-fdata-sections', '-fno-tree-loop-distribute-patterns')


def benchmark_sources(source):
    actual = tuple(sorted(path.name for path in (source / 'src').iterdir() if path.is_dir()))
    if actual != BENCHMARKS:
        missing = sorted(set(BENCHMARKS) - set(actual))
        unexpected = sorted(set(actual) - set(BENCHMARKS))
        raise ValueError(f'Embench-IoT inventory changed; missing {missing}; unexpected {unexpected}')
    sources = {}
    for benchmark in BENCHMARKS:
        files = tuple(sorted((source / 'src' / benchmark).glob('*.c')))
        if not files:
            raise ValueError(f'Embench-IoT benchmark has no C sources: {benchmark}')
        sources[benchmark] = files
    return sources


def verify_upstream(source):
    required = [source / 'sconstruct.py', *(source / path for path in SUPPORT_SOURCES)]
    required += [source / 'support/support.h', source / 'support/beebsc.h']
    sources = benchmark_sources(source)
    required += [path for files in sources.values() for path in files]
    if any(not path.is_file() for path in required):
        raise ValueError('Embench-IoT checkout is missing a required upstream source')
    relative = [str(path.relative_to(source)) for path in required]
    subprocess.run(['git', '-C', str(source), 'ls-files', '--error-unmatch', *relative],
                   check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['git', '-C', str(source), 'diff', '--quiet', 'HEAD'], check=True)
    return sources


def write_linker(template, destination, region):
    text = template.read_text().replace('@RAM_ORIGIN@', hex(region['base']))
    text = text.replace('@RAM_LENGTH@', hex(region['size']))
    destination.write_text(text)


def xgboost_functional_source(text):
    profile = FUNCTIONAL_PROFILES['xgboost']
    indices = ', '.join(str(index) for index in profile['sample_indices'])
    replacements = (
        ('// Run inference with all samples specified in xgboost.c',
         '// Run Rhodium\'s bounded functional sample profile.'),
        ('        size_t correct = 0;',
         f'        static const size_t functional_samples[] = {{{indices}}};\n'
         '        size_t correct = 0;'),
        ('for (volatile size_t i = 0; i < SAMPLES_IN_FILE; i++)',
         'for (volatile size_t i = 0; i < sizeof(functional_samples) / sizeof(functional_samples[0]); i++)'),
        ('uint8_t predicted = predict(X_test[i]);',
         'uint8_t predicted = predict(X_test[functional_samples[i]]);'),
        ('uint8_t label = Y_test[i];',
         'uint8_t label = Y_test[functional_samples[i]];'),
        ('// r is the number of errors therefore if r = 0 then output a 1 for correct',
         '// Require the pinned model\'s exact correct count for this functional profile.'),
        ('return r >= SAMPLES_IN_FILE * (LOCAL_SCALE_FACTOR, GLOBAL_SCALE_FACTOR / 12);',
         f'return r == {profile["expected_correct"]} * LOCAL_SCALE_FACTOR * GLOBAL_SCALE_FACTOR;'),
    )
    for original, replacement in replacements:
        count = text.count(original)
        if count != 1:
            raise ValueError(f'xgboost: expected one functional-profile source marker, found {count}')
        text = text.replace(original, replacement)
    return text


def materialize_sources(sources, destination, local_scale):
    generated = {}
    upstream_scales = {}
    for benchmark, paths in sources.items():
        generated[benchmark] = []
        replacements = 0
        for path in paths:
            text = path.read_text()

            def replace(match):
                nonlocal replacements
                replacements += 1
                upstream_scales[benchmark] = int(match.group(1))
                return f'#define LOCAL_SCALE_FACTOR {local_scale}'

            text = re.sub(r'^#define LOCAL_SCALE_FACTOR\s+(\d+)\s*$', replace, text,
                          flags=re.MULTILINE)
            if benchmark == 'xgboost' and path.name == 'testbench.c':
                text = xgboost_functional_source(text)
            output = destination / benchmark / path.name
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(text)
            generated[benchmark].append(output)
        if replacements != 1:
            raise ValueError(f'{benchmark}: expected one upstream LOCAL_SCALE_FACTOR, found {replacements}')
        if benchmark == 'xgboost' and not any(path.name == 'testbench.c' for path in paths):
            raise ValueError('xgboost: expected upstream testbench.c')
    return generated, upstream_scales


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--port', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--scale', type=int, default=1)
    parser.add_argument('--local-scale', type=int, default=1)
    parser.add_argument('--warmup-heat', type=int, default=0)
    args = parser.parse_args()
    if args.scale <= 0 or args.local_scale <= 0 or args.warmup_heat < 0:
        parser.error('global and local scales must be positive and warmup heat must be nonnegative')
    source, port, output = args.source.resolve(), args.port.resolve(), args.output.resolve()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error(f'compiler not found: {args.compiler}')
    try:
        target = load_target(args.target)
        sources = verify_upstream(source)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    if target['xlen'] != 64:
        parser.error('the initial Embench-IoT bare-metal port requires RV64')
    output.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output([compiler, '--version'], text=True)
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    port_inputs = [port / name for name in ('boardsupport.c', 'boardsupport.h', 'htif.c', 'start.S', 'link.ld.in')]
    if any(not path.is_file() for path in port_inputs):
        parser.error('Embench-IoT port is incomplete')
    key_input = (revision + version + str(source) + compiler + str(args.scale)
                 + str(args.local_scale)
                 + str(args.warmup_heat) + json.dumps(target, sort_keys=True)).encode()
    for path in [Path(__file__), Path(__file__).with_name('program_target.py'), *port_inputs]:
        key_input += path.read_bytes()
    key = hashlib.sha256(key_input).hexdigest()
    build = output / 'build' / key
    build.mkdir(parents=True, exist_ok=True)
    for generated in ('manifest.json', 'instruction-report.json'):
        (output / generated).unlink(missing_ok=True)
    linker = build / 'link.ld'
    write_linker(port / 'link.ld.in', linker, target['ram'][0])
    generated_sources, upstream_scales = materialize_sources(
        sources, build / 'functional-sources', args.local_scale)
    compiler_arch = probe_compiler(compiler, target['march'], target['mabi'], build)
    common = [compiler, *COMMON_FLAGS, f'-march={target["march"]}', f'-mabi={target["mabi"]}',
              f'-I{source / "support"}', f'-I{port}', '-DHAVE_BOARDSUPPORT_H=1',
              f'-DWARMUP_HEAT={args.warmup_heat}', f'-DGLOBAL_SCALE_FACTOR={args.scale}']
    names = [benchmark + '.riscv' for benchmark in BENCHMARKS]
    stamp = build / 'built.json'
    try:
        cached = json.loads(stamp.read_text())
    except (OSError, ValueError):
        cached = {}
    reuse = isinstance(cached, dict) and set(cached) == set(names) and all(
        (build / name).is_file()
        and hashlib.sha256((build / name).read_bytes()).hexdigest() == cached[name]
        for name in names)
    readelf = readelf_for(compiler)
    objdump = objdump_for(compiler)
    tests = []
    report = []
    with (output / 'build.log').open('w') as log:
        if reuse:
            log.write(f'Reusing {len(names)} checksum-verified ELFs for {key}\n')
        else:
            for benchmark, name in zip(BENCHMARKS, names):
                elf = build / name
                command = [*common, f'-I{source / "src" / benchmark}', str(port / 'start.S'),
                           *(str(path) for path in generated_sources[benchmark]),
                           *(str(source / path) for path in SUPPORT_SOURCES),
                           str(port / 'boardsupport.c'), str(port / 'htif.c'), '-nostdlib',
                           '-nostartfiles', '-no-pie', f'-Wl,-T,{linker}', '-Wl,--gc-sections',
                           '-Wl,--start-group', '-lc', '-lm', '-lgcc', '-Wl,--end-group',
                           '-o', str(elf)]
                log.write(' '.join(command) + '\n')
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
                if result.returncode:
                    raise RuntimeError(f'{benchmark} build failed; see {output / "build.log"}')
        for benchmark, name in zip(BENCHMARKS, names):
            elf = build / name
            if not elf.is_file():
                raise RuntimeError(f'selected ELF is missing: {elf}')
            elf_arch = elf_architecture(readelf, elf)
            if elf_arch != compiler_arch:
                raise RuntimeError(f'{elf}: ISA attributes {elf_arch} do not match {compiler_arch}')
            test = dict(name=benchmark, elf=str(elf.relative_to(output)),
                        sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), elf_arch=elf_arch,
                        load_segments=check_elf_memory(elf, target['ram']))
            tests.append(test)
            report.append(dict(name=benchmark, elf_arch=elf_arch,
                               **instruction_inventory(objdump, elf)))
    manifest = dict(suite='embench', revision=revision, compiler=version, cache_key=key,
                    compiler_flags=' '.join((*COMMON_FLAGS, f'-march={target["march"]}',
                                             f'-mabi={target["mabi"]}')),
                    mode='functional', scoring=False, scale=args.scale,
                    local_scale=args.local_scale, upstream_local_scales=upstream_scales,
                    functional_profiles=FUNCTIONAL_PROFILES,
                    warmup_heat=args.warmup_heat, march=target['march'], mabi=target['mabi'],
                    compiler_arch=compiler_arch, target=target,
                    target_fingerprint=target_fingerprint(target), tests=tests)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'instruction-report.json').write_text(json.dumps({
        'march': target['march'], 'mabi': target['mabi'], 'compiler_arch': compiler_arch,
        'binaries': report,
    }, indent=2) + '\n')
    stamp.write_text(json.dumps({test['name'] + '.riscv': test['sha256'] for test in tests}, indent=2) + '\n')
    print(f'Built {len(tests)} Embench-IoT functional workloads; manifest: {output / "manifest.json"}')


if __name__ == '__main__':
    main()
