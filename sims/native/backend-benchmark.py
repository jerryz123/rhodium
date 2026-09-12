#!/usr/bin/env python3
# Measures compiled backend changes against matching Verilator binaries without re-elaborating RTL.
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import statistics
import subprocess
from inspect_ir import inspect_binary

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--verilator-1', type=Path)
    parser.add_argument('--verilator-4', type=Path)
    parser.add_argument('--verilator-source-1', type=Path)
    parser.add_argument('--verilator-source-4', type=Path)
    parser.add_argument('--cc', help='Use one compiler for both native runtime and generated C')
    parser.add_argument('--cxx', default=os.getenv('CXX', 'c++'))
    parser.add_argument('--cflags', help='Use identical optimization/ISA flags for all rebuilt code')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runtime-cc', default=os.getenv('CC', 'cc'))
    parser.add_argument('--generated-cc', default=os.getenv('CC', 'cc'))
    parser.add_argument('--runtime-cflags', default='-O2')
    parser.add_argument('--generated-cflags', default='-O2')
    parser.add_argument('--trials', type=int, default=5)
    parser.add_argument('--short-boots', type=int, default=500)
    parser.add_argument('--long-boots', type=int, default=1000)
    args = parser.parse_args()
    if args.cc:
        args.runtime_cc = args.generated_cc = args.cc
    if args.cflags:
        args.runtime_cflags = args.generated_cflags = args.cflags
    rebuild = args.verilator_source_1 is not None or args.verilator_source_4 is not None
    if rebuild:
        if not (args.verilator_source_1 and args.verilator_source_4) or args.verilator_1 or args.verilator_4:
            parser.error('supply both generated source directories, without prebuilt executables')
        if shlex.split(args.runtime_cflags) != shlex.split(args.generated_cflags):
            parser.error('rebuilt comparisons require identical native runtime/generated flags')
        versions = [subprocess.check_output([cc, '--version'], text=True).splitlines()[0]
                    for cc in (args.runtime_cc, args.generated_cc, args.cxx)]
        releases = [subprocess.check_output([cc, '-dumpversion'], text=True).strip()
                    for cc in (args.runtime_cc, args.generated_cc, args.cxx)]
        if len(set(releases)) != 1 or len(set('clang' in v.lower() for v in versions)) != 1:
            parser.error('rebuilt comparisons require matching C/C++ compiler family and version')
    elif not (args.verilator_1 and args.verilator_4):
        parser.error('supply matching executables or generated source directories for both worker counts')
    if min(args.trials, args.short_boots, args.long_boots) < 1:
        parser.error('trials and boot counts must be positive')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    model = args.model.resolve()
    env = {k: v for k, v in os.environ.items() if not k.startswith('RDS_')}
    if rebuild:
        # Ambient make/compile overrides must not defeat the recorded matching flags.
        for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS'):
            env.pop(name, None)
    cpus, cores = [], set()
    for cpu in sorted(os.sched_getaffinity(0)):
        topology = Path(f'/sys/devices/system/cpu/cpu{cpu}/topology')
        core = tuple((topology / f).read_text().strip() for f in ('physical_package_id', 'core_id'))
        if core not in cores:
            cores.add(core)
            cpus.append(cpu)
    if len(cpus) < 4:
        parser.error('four available physical cores are required')
    cpus = cpus[:4]
    sources = sorted((ROOT / 'rhodium/sim/runtime').glob('*.c'))
    runtime_flags = shlex.split(args.runtime_cflags) + ['-DNDEBUG']
    generated_flags = shlex.split(args.generated_cflags) + ['-DNDEBUG']
    commands = []

    def run(cmd, log, extra=None):
        cmd = list(map(str, cmd))
        commands.append(cmd)
        with (out / log).open('w') as stream:
            subprocess.run(cmd, env=env | (extra or {}), stdout=stream, stderr=subprocess.STDOUT, check=True)

    for name in ('compile-model', 'mini-smoke'):
        run([args.runtime_cc, '-std=c17', '-pthread', *runtime_flags, '-Wall', '-Wextra', '-Werror',
             ROOT / 'sims/native' / f'{name}.c', *sources, '-ldl', '-o', out / name], f'{name}-build.log')
    modes = {}
    artifacts = (sources + sorted((ROOT / 'rhodium/sim/runtime').rglob('*.h'))
                 + [ROOT / 'sims/native' / name for name in
                    ('compile-model.c', 'mini-smoke.c', 'mini-loader.h', 'backend-benchmark.py', 'inspect_ir.py')]
                 + [out / 'compile-model', out / 'mini-smoke', model])
    if rebuild:
        artifacts.append(ROOT / 'sims/native/verilator-smoke.cpp')
        for workers, original in ((1, args.verilator_source_1), (4, args.verilator_source_4)):
            target = out / f'verilated-{workers}'
            target.mkdir(exist_ok=True)
            originals = [f for f in original.resolve().iterdir() if f.suffix in ('.cpp', '.h', '.mk')]
            for f in originals:
                shutil.copy2(f, target / f.name)
            artifacts += [target / f.name for f in originals]
            flags = shlex.join(generated_flags)
            run(['make', '-B', '-C', target, '-f', 'VSoCHarness.mk', '-j', '2',
                 f'CXX={args.cxx}', f'LINK={args.cxx}', 'CXXFLAGS=',
                 f'OPT_FAST={flags}', f'OPT_SLOW={flags}', f'OPT_GLOBAL={flags}',
                 f'VM_USER_CFLAGS=-DNDEBUG -DRDS_VERILATOR_THREADS={workers}',
                 f'VM_USER_DIR={ROOT / "sims/native"}'], f'verilator-{workers}-build.log')
            setattr(args, f'verilator_{workers}', target / 'VSoCHarness')
    for workers, flags in ((1, 331792), (4, 348752)):
        source, binary = out / f'native-{workers}.c', out / f'native-{workers}.so'
        config = dict(RDS_WORKERS=str(workers), RDS_FLAGS=str(flags))
        run([out / 'compile-model', model, source], f'native-{workers}-emit.log',
            config | {'RDS_PLAN_REPORT': str(out / f'native-{workers}.plan.json')})
        run([args.generated_cc, '-std=c17', *generated_flags, '-fPIC', '-shared', source, '-o', binary],
            f'native-{workers}-build.log')
        native_env = env | config | {'RDS_COMPILED': str(binary)}
        stats = json.loads(subprocess.check_output([out / 'mini-smoke', model, '0', 'stats'], env=native_env, text=True))
        if stats['workers'] != workers:
            raise RuntimeError(f'actual native workers differ: {stats}')
        (out / f'native-{workers}.stats.json').write_text(json.dumps(stats, indent=2) + '\n')
        modes[f'native-{workers}'] = ([str(out / 'mini-smoke'), str(model)], native_env, workers)
        verilator = (args.verilator_1 if workers == 1 else args.verilator_4).resolve()
        modes[f'verilator-{workers}'] = ([str(verilator)], env, workers)
        artifacts += [source, binary, verilator]
    target_macros = subprocess.check_output(
        [args.generated_cc, *generated_flags, '-dM', '-E', '-x', 'c', '/dev/null'], text=True)
    zen4 = '#define __znver4__ ' in target_macros
    machine_audits = {}
    if zen4:
        for name, (command, config, _) in modes.items():
            binary = Path(config['RDS_COMPILED']) if name.startswith('native') else Path(command[0])
            audit = inspect_binary(binary)
            machine_audits[name] = audit
            if audit['zen4_compress_stores']:
                raise RuntimeError(f'{name}: Zen 4 memory-destination compression; use register compression followed by a bounded store: {audit["zen4_compress_stores"][:4]}')
        (out / 'zen4-machine-code.json').write_text(json.dumps(machine_audits, indent=2) + '\n')
    # Finish all builds before timing and rotate engine/worker order between trials.
    rows, signatures = [], {}
    for loop, boots in ((0, args.short_boots), (256, args.long_boots)):
        for trial in range(args.trials):
            names = list(modes)
            names = names[trial % len(names):] + names[:trial % len(names)]
            if trial % 2:
                names.reverse()
            for name in names:
                cmd, config, workers = modes[name]
                command = ['taskset', '-c', ','.join(map(str, cpus[:workers])), *cmd, str(boots), 'bench']
                row = json.loads(subprocess.check_output(command, env=config | {'RDS_LOOP_ITERATIONS': str(loop)}, text=True))
                signature = row['cycles'], row['polls'], row['digest']
                if signature != signatures.setdefault(loop, signature):
                    raise RuntimeError(f'functional mismatch: {name}, {signature}, {signatures[loop]}')
                if row['workers'] != workers or (name.startswith('verilator') and row['context_threads'] != workers):
                    raise RuntimeError(f'thread mismatch: {name}, {row}')
                row.update(mode=name, loop=loop, trial=trial, boots=boots, rate=row['cycles'] / row['seconds'])
                rows.append(row)
            print(f'completed loop={loop} trial={trial}', flush=True)
    medians = {str(loop): {name: statistics.median(r['rate'] for r in rows if r['loop'] == loop and r['mode'] == name)
                          for name in modes} for loop in (0, 256)}
    report = dict(rows=rows, medians=medians, zen4_compress_store_audit=zen4, cpus=cpus, warmup_boots=5, build_commands=commands,
                  runtime_cflags=runtime_flags, generated_cflags=generated_flags,
                  runtime_compiler=subprocess.check_output([args.runtime_cc, '--version'], text=True).splitlines()[0],
                  generated_compiler=subprocess.check_output([args.generated_cc, '--version'], text=True).splitlines()[0],
                  verilator_compiler=(subprocess.check_output([args.cxx, '--version'], text=True).splitlines()[0]
                                      if rebuild else 'external executable; flags not independently verified'),
                  verilator_cflags=generated_flags if rebuild else None,
                  source_commit=subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
                  sha256={str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in artifacts})
    (out / 'benchmark.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(medians, indent=2))


if __name__ == '__main__':
    main()
