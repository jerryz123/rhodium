#!/usr/bin/env python3
# Builds target-bound litmus7 ELFs and manifests from pinned RISC-V model outcomes.
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
from program_target import (elf_architecture, load_target, probe_compiler, readelf_for,
                            target_fingerprint, elf_build_metadata)

# The upstream model log keys outcomes by name, not path. Resolve only the
# duplicate names whose source variant we explicitly select here.
DUPLICATE_CASES = {
    'MP': 'non-mixed-size/BASIC_2_THREAD/MP.litmus',
    'SB': 'non-mixed-size/BASIC_2_THREAD/SB.litmus',
    'LB': 'non-mixed-size/BASIC_2_THREAD/LB.litmus',
}


def listed_cases(source):
    def expand(index, active):
        if index in active:
            raise ValueError(f'cyclic litmus inventory: {index}')
        for entry in index.read_text().splitlines():
            entry = entry.strip()
            if not entry or entry.startswith('#'):
                continue
            path = index.parent / entry
            if path.name.startswith('@'):
                yield from expand(path, active | {index})
            elif path.suffix == '.litmus':
                yield path
            else:
                raise ValueError(f'unsupported litmus inventory entry: {path}')

    yield from expand(source / 'tests/non-mixed-size/@all', set())


def discover_litmus7_cases(source, model, available_harts):
    lines = model.read_text().splitlines()
    outcomes = {}
    index = 0
    while index < len(lines):
        match = re.fullmatch(r'Test (.+) (?:Allowed|Forbidden)', lines[index])
        if not match or index + 1 >= len(lines):
            index += 1
            continue
        count = re.fullmatch(r'States (\d+)', lines[index + 1])
        if not count:
            raise ValueError(f'{match[1]}: malformed pinned Herd state inventory')
        amount = int(count[1])
        states = lines[index + 2:index + 2 + amount]
        if len(states) != amount or index + 2 + amount >= len(lines) or lines[index + 2 + amount] not in ('Ok', 'No'):
            raise ValueError(f'{match[1]}: incomplete pinned Herd state inventory')
        if match[1] in outcomes:
            raise ValueError(f'{match[1]}: duplicate pinned Herd state inventory')
        outcomes[match[1]] = states
        index += 3 + amount
    candidates = {}
    for path in listed_cases(source):
        text = path.read_text()
        name = re.match(r'RISCV (\S+)', text)
        table = re.search(r'(?m)^\s*P0(?:\s*\|\s*P\d+)*\s*;\s*$', text)
        if not name or not table or name[1] not in outcomes or not outcomes[name[1]]:
            continue
        harts = [int(number) for number in re.findall(r'P(\d+)', table[0])]
        if harts != list(range(len(harts))) or len(harts) > available_harts:
            continue
        candidates.setdefault(name[1], []).append((path, len(harts), outcomes[name[1]]))
    cases = {}
    for name, entries in candidates.items():
        if len(entries) == 1:
            cases[name] = entries[0]
        elif name in DUPLICATE_CASES:
            chosen = source / 'tests' / DUPLICATE_CASES[name]
            matches = [entry for entry in entries if entry[0] == chosen]
            if len(matches) == 1:
                cases[name] = matches[0]
        elif len({entry[0].read_bytes() for entry in entries}) == 1:
            cases[name] = min(entries, key=lambda entry: entry[0].as_posix())
    return cases


ZALASR_INSTRUCTION = re.compile(r'\b(?:l[bhwd]\.aq|s[bhwd]\.rl)\b')


def supported_litmus7_cases(cases, extensions):
    if 'zalasr' in extensions:
        return cases
    return {name: entry for name, entry in cases.items()
            if not ZALASR_INSTRUCTION.search(entry[0].read_text())}


def adapt_litmus7_runtime(path):
    source = path.read_text()
    declaration = 'typedef struct {\n  volatile int c,sense;\n  int n ;\n} sense_t;'
    initialization = '  p->n = p->c = n;\n  p->sense = 0;'
    flush = '  fflush(out);'
    barrier = re.compile(r'__attribute__ \(\(noinline\)\) static void barrier_wait\(sense_t \*p\) \{\n.*?\n\}', re.S)
    matches = barrier.findall(source)
    if (source.count(declaration) != 1 or source.count(initialization) != 1 or
            source.count(flush) != 1 or
            len(matches) != 1 or '  int rem = __sync_add_and_fetch(&p->c,-1) ;' not in matches[0]):
        raise ValueError(f'{path}: unrecognized litmus7 barrier template')
    source = source.replace(declaration,
                            'extern void litmus_baremetal_barrier_init(void *, int);\n'
                            'extern void litmus_baremetal_barrier_wait(void *);\n' + declaration)
    source = source.replace(initialization,
                            initialization + '\n  litmus_baremetal_barrier_init(p,n);')
    source = barrier.sub('__attribute__ ((noinline)) static void barrier_wait(sense_t *p) {\n'
                         '  litmus_baremetal_barrier_wait(p);\n}', source)
    source = source.replace(flush, '  /* Bare-metal emit_char writes directly to HTIF. */')
    path.write_text(source)


def litmus7_codegen_march(extensions):
    """Keep litmus support code scalar without changing generated test instructions."""
    supported = set(extensions)
    arch = 'rv64' + ''.join(ext for ext in 'imafdc' if ext in supported)
    return arch + ''.join('_' + ext for ext in ('zicsr', 'zifencei', 'zalasr') if ext in supported)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--riscv-tests', type=Path, required=True)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--runs', type=int, default=10)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--tests', default='all')
    selection.add_argument('--tests-file', type=Path, help='newline-delimited names for a fixed suite selection')
    parser.add_argument('--litmus7', required=True, help='explicit litmus7 executable for generated C tests')
    parser.add_argument('--litmus7-libdir', type=Path, help='litmus7 source-build support directory')
    parser.add_argument('--keep-going', action='store_true', help='record case build failures and continue the batch build')
    args = parser.parse_args()
    if args.runs <= 0:
        parser.error('--runs must be positive')
    target = load_target(args.target)
    if target['xlen'] != 64 or 'i' not in target['extensions'] or target['harts'] != list(range(len(target['harts']))):
        parser.error('litmus runtime requires contiguous RV64I harts starting at zero')
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error(f'compiler not found: {args.compiler}')
    litmus7 = shutil.which(args.litmus7)
    if not litmus7:
        parser.error(f'litmus7 not found: {args.litmus7}')
    if args.litmus7_libdir and not args.litmus7_libdir.is_dir():
        parser.error('--litmus7-libdir requires an existing directory')
    source, runtime, riscv_tests = args.source.resolve(), args.runtime.resolve(), args.riscv_tests.resolve()
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    subprocess.run(['git', '-C', str(source), 'diff', '--quiet', 'HEAD'], check=True)
    riscv_tests_revision = subprocess.check_output(
        ['git', '-C', str(riscv_tests), 'rev-parse', 'HEAD'], text=True).strip()
    model = source / 'model-results/herd.logs'
    candidates = discover_litmus7_cases(source, model, len(target['harts']))
    cases = supported_litmus7_cases(candidates, target['extensions'])
    if args.tests_file:
        selected = [line.strip() for line in args.tests_file.read_text().splitlines()
                    if line.strip() and not line.lstrip().startswith('#')]
    else:
        selected = sorted(cases) if args.tests == 'all' else args.tests.split(',')
    if not selected or len(set(selected)) != len(selected) or any(name not in cases for name in selected):
        parser.error('unknown, unsupported, or duplicate litmus test')
    common = riscv_tests / 'benchmarks/common'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output([compiler, '--version'], text=True)
    codegen_march = litmus7_codegen_march(target['extensions'])
    compiler_arch = probe_compiler(compiler, codegen_march, target['mabi'], output)
    metadata = elf_build_metadata(target, 'litmus', dict(runs=args.runs, tests=selected, generator='litmus7'))
    key_bytes = (revision + riscv_tests_revision + version + compiler + metadata['build_spec_fingerprint']
                 + codegen_march).encode()
    litmus7_digest = hashlib.sha256(Path(litmus7).read_bytes()).hexdigest()
    key_bytes += litmus7_digest.encode()
    key_bytes += str(args.litmus7_libdir or '').encode()
    if args.litmus7_libdir:
        for support in sorted(args.litmus7_libdir.rglob('*')):
            if support.is_file():
                key_bytes += support.relative_to(args.litmus7_libdir).as_posix().encode()
                key_bytes += hashlib.sha256(support.read_bytes()).digest()
    for path in (Path(__file__), runtime / 'link.ld.in',
                 runtime / 'litmus7-port.c', runtime / 'litmus7-main.c',
                 runtime / 'litmus7-streams.h', common / 'crt.S',
                 common / 'syscalls.c', common / 'util.h',
                 riscv_tests / 'env/encoding.h', model):
        key_bytes += hashlib.sha256(path.read_bytes()).digest()
    for name in selected:
        key_bytes += cases[name][0].read_bytes()
    key = hashlib.sha256(key_bytes).hexdigest()
    build = output / 'build' / key
    build.mkdir(parents=True, exist_ok=True)
    (output / 'manifest.json').unlink(missing_ok=True)
    tests = []
    build_failures = []
    with (output / 'build.log').open('w') as log:
        for name in selected:
            path, hart_count, model_outcomes = cases[name]
            case_dir = build / name
            case_dir.mkdir(exist_ok=True)
            linker = case_dir / 'link.ld'
            linker.write_text(runtime.joinpath('link.ld.in').read_text()
                              .replace('@RAM_ORIGIN@', hex(target['ram'][0]['base']))
                              .replace('@RAM_LENGTH@', hex(target['ram'][0]['size']))
                              .replace('@STACK_BYTES@', str(hart_count * 128 * 1024)))
            crt = common.joinpath('crt.S').read_text()
            marker = '  # for now, assume only 1 core\n  li a1, 1\n'
            if crt.count(marker) != 1:
                raise RuntimeError('upstream multihart startup marker changed')
            (case_dir / 'crt.S').write_text(crt.replace(marker, f'  # Boot selected litmus harts\n  li a1, {hart_count}\n'))
            elf = case_dir / f'{name}.elf'
            flags = [compiler, '-O2', '-mcmodel=medany', '-static', '-fno-common', '-fno-pie',
                     f'-march={codegen_march}', f'-mabi={target["mabi"]}',
                     f'-I{riscv_tests / "env"}', f'-I{common}']
            generated = case_dir / 'litmus7'
            test_object = case_dir / 'litmus7-test.o'
            command = flags + [f'-I{generated}', f'-DLITMUS_HARTS={hart_count}',
                               f'-DLITMUS_CLOCK_HZ={target["clock_frequency_hz"]}',
                               str(case_dir / 'crt.S'), str(common / 'syscalls.c'),
                               str(runtime / 'litmus7-port.c'), str(runtime / 'litmus7-main.c'),
                               str(generated / 'litmus_io.c'), str(generated / 'litmus_rand.c'),
                               str(test_object), '-nostartfiles', '-no-pie',
                               f'-Wl,-T,{linker}', '-lm', '-lc', '-lgcc', '-o', str(elf)]
            stamp = case_dir / 'built.sha256'
            digest = hashlib.sha256(elf.read_bytes()).hexdigest() if elf.is_file() else None
            if digest and stamp.is_file() and stamp.read_text().strip() == digest:
                log.write(f'Reusing checksum-verified ELF {elf}\n')
            else:
                generated.mkdir(exist_ok=True)
                generate = [litmus7, '-mode', 'presi', '-driver', 'shell', '-carch', 'RISCV',
                            '-alloc', 'static', '-barrier', 'userfence', '-avail',
                            str(hart_count), '-s', '1', '-r', str(args.runs),
                            '-o', str(generated), str(path)]
                if args.litmus7_libdir:
                    generate[1:1] = ['-set-libdir', str(args.litmus7_libdir.resolve())]
                log.write(' '.join(generate) + '\n')
                result = subprocess.run(generate, stdout=log, stderr=subprocess.STDOUT)
                if result.returncode or not (generated / f'{name}.c').is_file():
                    if not args.keep_going:
                        raise RuntimeError(f'{name}: litmus7 generation failed; see {output / "build.log"}')
                    build_failures.append(dict(name=name, source=str(path.relative_to(source)), stage='generation'))
                    continue
                try:
                    adapt_litmus7_runtime(generated / f'{name}.c')
                except ValueError:
                    if not args.keep_going:
                        raise
                    build_failures.append(dict(name=name, source=str(path.relative_to(source)), stage='runtime adaptation'))
                    continue
                compile_test = flags + [f'-I{generated}', '-Dmain=litmus_generated_main',
                                        '-fno-builtin-fprintf', '-include',
                                        str(runtime / 'litmus7-streams.h'),
                                        '-c', str(generated / f'{name}.c'), '-o', str(test_object)]
                log.write(' '.join(compile_test) + '\n')
                result = subprocess.run(compile_test, stdout=log, stderr=subprocess.STDOUT)
                if result.returncode:
                    if not args.keep_going:
                        raise RuntimeError(f'{name}: litmus7 compilation failed; see {output / "build.log"}')
                    build_failures.append(dict(name=name, source=str(path.relative_to(source)), stage='test compilation'))
                    continue
                log.write(' '.join(command) + '\n')
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
                if result.returncode:
                    if not args.keep_going:
                        raise RuntimeError(f'{name}: build failed; see {output / "build.log"}')
                    build_failures.append(dict(name=name, source=str(path.relative_to(source)), stage='link'))
                    continue
                digest = hashlib.sha256(elf.read_bytes()).hexdigest()
                stamp.write_text(digest + '\n')
            elf_arch = elf_architecture(readelf_for(compiler), elf)
            if elf_arch != compiler_arch:
                raise RuntimeError(f'{name}: ELF ISA attributes do not match target compiler arch')
            entry = dict(name=name, source=str(path.relative_to(source)),
                              elf=str(elf.relative_to(output)),
                              sha256=digest,
                              elf_arch=elf_arch, load_segments=check_elf_memory(elf, target['ram']),
                              harts=list(range(hart_count)),
                              required_output=[f'Test {name} ', 'Histogram ('],
                              forbidden_output=['LITMUS_HARNESS_ERROR'],
                              litmus_allowed_states=model_outcomes,
                              litmus_min_samples=args.runs)
            tests.append(entry)
    manifest = dict(suite='litmus', revision=revision, riscv_tests_revision=riscv_tests_revision,
                    model='pinned upstream herd.logs', generator='litmus7',
                    compiler=version, codegen_march=codegen_march, cache_key=key, runs=args.runs,
                    inventory_cases=len(candidates),
                    discovered_cases=len(cases),
                    attempted_cases=len(selected), build_failures=build_failures, target=target,
                    target_fingerprint=target_fingerprint(target), tests=tests, **metadata)
    manifest['litmus7_version'] = subprocess.check_output([litmus7, '-version'], text=True).strip()
    manifest['litmus7_sha256'] = litmus7_digest
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Built {len(tests)} of {len(selected)} litmus ELFs; '
          f'{len(build_failures)} build failures; manifest: {output / "manifest.json"}')
    if build_failures:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
