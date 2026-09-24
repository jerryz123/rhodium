#!/usr/bin/env python3
# Builds bounded bare-metal RISC-V litmus ELFs from pinned tests and model outcomes.
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
                            target_fingerprint)

# The upstream model log keys outcomes by name, not path. Resolve only the
# duplicate names whose source variant we intentionally qualify here.
DUPLICATE_CASES = {
    'MP': 'non-mixed-size/BASIC_2_THREAD/MP.litmus',
    'SB': 'non-mixed-size/BASIC_2_THREAD/SB.litmus',
    'LB': 'non-mixed-size/BASIC_2_THREAD/LB.litmus',
}
INSTRUCTION = re.compile(
    r'(?:lw|sw|ld|sd|xor|add)\s+x\d+,(?:x\d+|[-+]?\d+\(x\d+\))(?:,x\d+)?'
    r'|fence\s+[iorw]+,[iorw]+')
INIT = re.compile(r'(\d+):x(\d+)=([A-Za-z_][A-Za-z_0-9]*|[-+]?\d+)$')
FIELD = re.compile(r'(\d+):x(\d+)=([-+]?\d+);')
CALLEE_SAVED = (8, 9, *range(18, 28))


def parse_case(path):
    source = path.read_text()
    lines = source.splitlines()
    match = re.fullmatch(r'RISCV (\S+)', lines[0])
    if not match:
        raise ValueError(f'{path}: expected a RISC-V litmus header')
    name = match.group(1)
    block = re.search(r'\{(.*?)\}', source, re.S)
    if not block:
        raise ValueError(f'{name}: missing initial state')
    registers = {}
    for statement in block.group(1).split(';'):
        statement = statement.strip()
        if not statement or statement.startswith('uint64_t '):
            continue
        item = INIT.fullmatch(statement)
        if not item:
            raise ValueError(f'{name}: unsupported initial state {statement!r}')
        hart, reg, value = int(item[1]), int(item[2]), item[3]
        if reg < 5 or reg > 31:
            raise ValueError(f'{name}: register x{reg} is reserved by the runtime')
        if value not in ('x', 'y') and not re.fullmatch(r'[-+]?\d+', value):
            raise ValueError(f'{name}: unsupported initial value {value}')
        registers.setdefault(hart, []).append((reg, value))
    table = re.search(r'^\s*P0\s*\|.*?;\s*$', source, re.M)
    if not table:
        raise ValueError(f'{name}: missing thread table')
    harts = [int(number) for number in re.findall(r'P(\d+)', table.group())]
    if harts != list(range(len(harts))) or len(harts) < 2:
        raise ValueError(f'{name}: unsupported hart numbering')
    instructions = [[] for _ in harts]
    tail = source[table.end():]
    for line in tail.splitlines():
        if line.startswith('exists') or line.startswith('forall'):
            break
        if not line.strip():
            continue
        if not line.rstrip().endswith(';'):
            raise ValueError(f'{name}: malformed instruction row')
        cells = line.rstrip()[:-1].split('|')
        if len(cells) != len(harts):
            raise ValueError(f'{name}: instruction row has wrong width')
        for hart, cell in enumerate(cells):
            instruction = ' '.join(cell.split())
            if instruction:
                if not INSTRUCTION.fullmatch(instruction):
                    raise ValueError(f'{name}: unsupported instruction {instruction!r}')
                if any(0 < int(reg) < 5 for reg in re.findall(r'\bx(\d+)\b', instruction)):
                    raise ValueError(f'{name}: instruction uses a runtime-reserved register')
                instructions[hart].append(instruction)
    if any(not instructions[hart] or hart not in registers for hart in harts):
        raise ValueError(f'{name}: every hart needs instructions and initial registers')
    return name, registers, instructions


def model_states(path, name):
    text = path.read_text()
    match = re.search(rf'^Test {re.escape(name)} (?:Allowed|Forbidden)\nStates (\d+)\n(.*?)^(?:Ok|No)$',
                      text, re.M | re.S)
    if not match:
        raise ValueError(f'{name}: missing pinned Herd state inventory')
    states = []
    keys = None
    for line in match[2].splitlines():
        fields = FIELD.findall(line)
        if not fields or ' '.join(f'{hart}:x{reg}={value};' for hart, reg, value in fields) != line:
            raise ValueError(f'{name}: unsupported model state {line!r}')
        state_keys = [(int(hart), int(reg)) for hart, reg, _ in fields]
        if keys is None:
            keys = state_keys
        if state_keys != keys:
            raise ValueError(f'{name}: inconsistent model state fields')
        states.append([int(value) for _, _, value in fields])
    if len(states) != int(match[1]):
        raise ValueError(f'{name}: model state count mismatch')
    return keys, states


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


def discover_cases(source, model, available_harts):
    candidates = {}
    for path in listed_cases(source):
        try:
            name, registers, instructions = parse_case(path)
            fields, states = model_states(model, name)
        except ValueError:
            continue
        if len(instructions) <= available_harts:
            candidates.setdefault(name, []).append((path, registers, instructions, fields, states))
    cases = {}
    for name, entries in candidates.items():
        if len(entries) == 1:
            cases[name] = entries[0]
        elif name in DUPLICATE_CASES:
            chosen = source / 'tests' / DUPLICATE_CASES[name]
            matches = [entry for entry in entries if entry[0] == chosen]
            if len(matches) != 1:
                raise ValueError(f'{name}: selected duplicate source is unavailable')
            cases[name] = matches[0]
    return cases


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


def assembly(registers, instructions, fields):
    output = ['# Generated litmus instructions and observations; do not edit.',
              '# SPDX-License-Identifier: Apache-2.0', '.text']
    for hart, body in enumerate(instructions):
        observations = [(index, reg) for index, (owner, reg) in enumerate(fields) if owner == hart]
        frame = (104 + 8 * len(observations) + 15) & -16
        output += [f'.globl litmus_thread_{hart}', f'litmus_thread_{hart}:',
                   f'  addi sp,sp,-{frame}', '  sd a0,96(sp)']
        output += [f'  sd x{reg},{index * 8}(sp)' for index, reg in enumerate(CALLEE_SAVED)]
        for reg, value in registers[hart]:
            output.append(f'  la x{reg},litmus_{value}' if value in ('x', 'y')
                          else f'  li x{reg},{value}')
        output += [f'  {instruction}' for instruction in body]
        output += [f'  sd x{reg},{104 + offset * 8}(sp)'
                   for offset, (_, reg) in enumerate(observations)]
        output.append('  ld t0,96(sp)')
        for offset, (index, _) in enumerate(observations):
            output += [f'  ld t1,{104 + offset * 8}(sp)', f'  sd t1,{index * 8}(t0)']
        output += [f'  ld x{reg},{index * 8}(sp)' for index, reg in enumerate(CALLEE_SAVED)]
        output += [f'  addi sp,sp,{frame}', '  ret']
    return '\n'.join(output) + '\n'


def header(name, harts, fields, states, runs):
    lines = ['// Defines generated litmus threads and pinned allowed outcomes.',
             '// SPDX-License-Identifier: Apache-2.0', '#pragma once', '#include <stdint.h>',
             f'#define LITMUS_NAME "{name}"', f'#define LITMUS_HARTS {harts}',
             f'#define LITMUS_RUNS {runs}', f'#define LITMUS_OBSERVATIONS {len(fields)}',
             f'#define LITMUS_STATES {len(states)}']
    lines += [f'void litmus_thread_{hart}(uint64_t *observed);' for hart in range(harts)]
    lines += ['static void (*const litmus_threads[LITMUS_HARTS])(uint64_t *) = {',
              '  ' + ', '.join(f'litmus_thread_{hart}' for hart in range(harts)) + '};',
              'static const char *const litmus_fields[LITMUS_OBSERVATIONS] = {',
              '  ' + ', '.join(f'"{hart}:x{reg}"' for hart, reg in fields) + '};',
              'static const uint64_t litmus_allowed[LITMUS_STATES][LITMUS_OBSERVATIONS] = {']
    lines += ['  {' + ', '.join(str(value) + 'ULL' for value in state) + '},' for state in states]
    lines += ['};']
    return '\n'.join(lines) + '\n'


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
    parser.add_argument('--litmus7', help='use this explicit litmus7 executable for generated C tests')
    parser.add_argument('--litmus7-libdir', type=Path, help='litmus7 source-build support directory')
    parser.add_argument('--keep-going', action='store_true', help='record case build failures and continue the bulk qualification')
    args = parser.parse_args()
    if args.runs <= 0:
        parser.error('--runs must be positive')
    target = load_target(args.target)
    if target['xlen'] != 64 or 'i' not in target['extensions'] or target['harts'] != list(range(len(target['harts']))):
        parser.error('litmus runtime requires contiguous RV64I harts starting at zero')
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error(f'compiler not found: {args.compiler}')
    litmus7 = shutil.which(args.litmus7) if args.litmus7 else None
    if args.litmus7 and not litmus7:
        parser.error(f'litmus7 not found: {args.litmus7}')
    if args.litmus7_libdir and (not litmus7 or not args.litmus7_libdir.is_dir()):
        parser.error('--litmus7-libdir requires litmus7 and an existing directory')
    source, runtime, riscv_tests = args.source.resolve(), args.runtime.resolve(), args.riscv_tests.resolve()
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    subprocess.run(['git', '-C', str(source), 'diff', '--quiet', 'HEAD'], check=True)
    riscv_tests_revision = subprocess.check_output(
        ['git', '-C', str(riscv_tests), 'rev-parse', 'HEAD'], text=True).strip()
    model = source / 'model-results/herd.logs'
    candidates = (discover_litmus7_cases(source, model, len(target['harts'])) if litmus7 else
                  discover_cases(source, model, len(target['harts'])))
    cases = supported_litmus7_cases(candidates, target['extensions']) if litmus7 else candidates
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
    codegen_march = target['march']
    if litmus7:
        codegen_march = litmus7_codegen_march(target['extensions'])
    compiler_arch = probe_compiler(compiler, codegen_march, target['mabi'], output)
    key_bytes = (revision + riscv_tests_revision + version + compiler + json.dumps(target, sort_keys=True)
                 + codegen_march + str(args.runs) + ','.join(selected)).encode()
    litmus7_digest = None
    if litmus7:
        litmus7_digest = hashlib.sha256(Path(litmus7).read_bytes()).hexdigest()
        key_bytes += litmus7_digest.encode()
        key_bytes += str(args.litmus7_libdir or '').encode()
        if args.litmus7_libdir:
            for support in sorted(args.litmus7_libdir.rglob('*')):
                if support.is_file():
                    key_bytes += support.relative_to(args.litmus7_libdir).as_posix().encode()
                    key_bytes += hashlib.sha256(support.read_bytes()).digest()
    for path in (Path(__file__), runtime / 'runtime.c', runtime / 'link.ld.in',
                 common / 'crt.S', common / 'syscalls.c', common / 'util.h',
                 riscv_tests / 'env/encoding.h', model):
        key_bytes += hashlib.sha256(path.read_bytes()).digest()
    if litmus7:
        for path in (runtime / 'litmus7-port.c', runtime / 'litmus7-main.c'):
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
            if litmus7:
                path, hart_count, model_outcomes = cases[name]
            else:
                path, registers, instructions, fields, states = cases[name]
                hart_count = len(instructions)
            case_dir = build / name
            case_dir.mkdir(exist_ok=True)
            linker = case_dir / 'link.ld'
            linker.write_text(runtime.joinpath('link.ld.in').read_text()
                              .replace('@RAM_ORIGIN@', hex(target['ram'][0]['base']))
                              .replace('@RAM_LENGTH@', hex(target['ram'][0]['size']))
                              .replace('@STACK_BYTES@', str(hart_count * 128 * 1024)))
            if not litmus7:
                (case_dir / 'litmus_case.S').write_text(assembly(registers, instructions, fields))
                (case_dir / 'litmus_case.h').write_text(header(name, hart_count, fields, states, args.runs))
            crt = common.joinpath('crt.S').read_text()
            marker = '  # for now, assume only 1 core\n  li a1, 1\n'
            if crt.count(marker) != 1:
                raise RuntimeError('upstream multihart startup marker changed')
            (case_dir / 'crt.S').write_text(crt.replace(marker, f'  # Boot selected litmus harts\n  li a1, {hart_count}\n'))
            elf = case_dir / f'{name}.elf'
            flags = [compiler, '-O2', '-mcmodel=medany', '-static', '-fno-common', '-fno-pie',
                     f'-march={codegen_march}', f'-mabi={target["mabi"]}',
                     f'-I{riscv_tests / "env"}', f'-I{common}']
            if litmus7:
                generated = case_dir / 'litmus7'
                test_object = case_dir / 'litmus7-test.o'
                command = flags + [f'-I{generated}', f'-DLITMUS_HARTS={hart_count}',
                                   f'-DLITMUS_CLOCK_HZ={target["clock_frequency_hz"]}',
                                   str(case_dir / 'crt.S'), str(common / 'syscalls.c'),
                                   str(runtime / 'litmus7-port.c'), str(runtime / 'litmus7-main.c'),
                                   str(generated / 'litmus_io.c'), str(generated / 'litmus_rand.c'),
                                   str(test_object), '-nostartfiles', '-no-pie',
                                   f'-Wl,-T,{linker}', '-lm', '-lc', '-lgcc', '-o', str(elf)]
            else:
                command = flags + [f'-I{case_dir}', str(case_dir / 'crt.S'),
                                   str(common / 'syscalls.c'), str(runtime / 'runtime.c'),
                                   str(case_dir / 'litmus_case.S'), '-nostartfiles', '-no-pie',
                                   f'-Wl,-T,{linker}', '-lm', '-lc', '-lgcc', '-o', str(elf)]
            stamp = case_dir / 'built.sha256'
            digest = hashlib.sha256(elf.read_bytes()).hexdigest() if elf.is_file() else None
            if digest and stamp.is_file() and stamp.read_text().strip() == digest:
                log.write(f'Reusing checksum-verified ELF {elf}\n')
            else:
                if litmus7:
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
                              required_output=([f'Test {name} ', 'Histogram ('] if litmus7 else
                                               [f'LITMUS_OK {name} runs={args.runs} forbidden=0']),
                              forbidden_output=(['LITMUS_HARNESS_ERROR'] if litmus7 else
                                                ['LITMUS_FORBIDDEN', 'LITMUS_FAIL']))
            if litmus7:
                entry['litmus_allowed_states'] = model_outcomes
                entry['litmus_min_samples'] = args.runs
            tests.append(entry)
    manifest = dict(suite='litmus', revision=revision, riscv_tests_revision=riscv_tests_revision,
                    model='pinned upstream herd.logs', generator=('litmus7' if litmus7 else 'adapter'),
                    compiler=version, codegen_march=codegen_march, cache_key=key, runs=args.runs,
                    inventory_cases=len(candidates),
                    discovered_cases=len(cases),
                    attempted_cases=len(selected), build_failures=build_failures, target=target,
                    target_fingerprint=target_fingerprint(target), tests=tests)
    if not litmus7:
        manifest['supported_cases'] = len(cases)
    else:
        manifest['litmus7_version'] = subprocess.check_output([litmus7, '-version'], text=True).strip()
        manifest['litmus7_sha256'] = litmus7_digest
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Built {len(tests)} of {len(selected)} litmus ELFs; '
          f'{len(build_failures)} build failures; manifest: {output / "manifest.json"}')
    if build_failures:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
