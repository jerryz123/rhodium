# Generates Moon configuration from Rhodium inventories and already-produced target descriptors.
# SPDX-License-Identifier: Apache-2.0

import argparse
from itertools import product
import json
import os
from pathlib import Path
import platform
import shlex
import tempfile

from socs.configs.inputs import NATIVE_WRITER_INPUTS, groups as config_groups
from sims.model_inputs import emission_inputs
from .harness import BY_TEST, BY_SUITE
from .programs import build_options, native_build_env, NATIVE_COMPILER
from .inputs import ACT_TOOLS, RECIPE_CACHE_KEYS, RUNNER, RUNTIME, catalog_groups, group, patterns, recipe
from .policy import (ARCH_SHARD_COUNTS, BACKEND_SMOKE_CONFIGS, BACKEND_SMOKE_VARIANTS, DEFAULT_BACKEND,
                     PROGRAM_SHARD_COUNTS, ROOT, SELECTIONS, SOFTWARE_TESTS, TEST_CONFIGS,
                     arch_configs, linux_configs, model_variant, native_programs, platform_configs, simple_software, simulator_options, software_configs)

CATALOG = ROOT / '.moon/tasks/rhodium-generated.yml'
ARCHIVES = Path('.rhodium-cache/moon/archives')


def task_id(product):
    return product.replace(':', '--')


def archive_id(key):
    return 'archive--' + key


def instrumentation(soc):
    options = simulator_options(soc)
    return [f'{flag.upper()}={int(options[flag])}' for flag in ('cosim', 'stats')]


def configuration_inputs(soc):
    return group(*('config-' + key for key in config_groups(*SELECTIONS[soc][:2])))


def act_model(soc):
    return f"model:{soc}:{DEFAULT_BACKEND}:{simulator_options(soc)['variant']}"


def baseline(soc, suite):
    return f'sims/program-test/performance-baselines/{soc}/{suite}.json'


class Tasks:
    """Emit native Moon declarations; Moon itself resolves their dependencies."""
    def __init__(self):
        self.nodes = {}

    def add(self, key, command=None, *, inputs=(), deps=(), hosted=False, extends=None, shards=None, env=None, outputs=None):
        executable, *args = command or [None]
        node = dict(deps=[task_id(d) if isinstance(d, str) else d for d in deps],
                    inputs=sorted({g for p in inputs for g in patterns(p)}))
        if extends:
            node.update(extends=task_id(extends), options={'mergeArgs': 'replace'})
        else:
            node.update(command=executable, options={'cache': False})
        if executable is not None and extends:
            node['command'] = executable
        if env:
            node['env'] = env
            node['options']['envOverride'] = True
        if outputs is not None:
            node['outputs'] = outputs
            node['options']['cache'] = True
        if hosted:
            node['options']['runInCI'] = True
        if 'racket-cache' in deps:
            node['options']['mutex'] = 'racket-cache'

        if shards is None:
            if command is not None:
                node['args'] = args
            if hosted:
                node['tags'] = ['ci-root', 'ci-receipt']
        else:
            children = []
            for n, flags in enumerate(shards):
                child = task_id(f'{key}:shard-{n}')
                self.nodes[child] = dict(node, env={**(env or {}), **flags})
                if hosted:
                    self.nodes[child]['tags'] = ['ci-receipt']
                children.append(child)
            node = dict(command='noop', inputs=[], deps=children)
            if hosted:
                node['tags'] = ['ci-root']
        self.nodes[task_id(key)] = node
        return key


def opensbi_stages(tasks, key, env, deps=()):
    """Instantiate the layout fork once for each configured or shared producer."""
    directory = '.rhodium-cache/moon/opensbi/' + task_id(key)
    env.setdefault('RHODIUM_BUILD_SUBDIR', 'opensbi')
    env['RHODIUM_OPENSBI_STAGE'] = directory
    layout = tasks.add(key + ':layout', extends='opensbi-layout', env=env, deps=deps,
                       outputs=[directory + '/layout.json'])
    tasks.nodes[task_id(layout)]['options']['cache'] = False
    firmware = tasks.add(key + ':firmware', extends='opensbi-firmware', env=env, deps=[layout])
    smoke = tasks.add(key + ':smoke-elf', extends='opensbi-smoke-elf', env=env, deps=[layout],
                     outputs=[directory + '/opensbi_smoke.elf'])
    return [firmware, smoke]


def archive_bindings(tasks, directory, local):
    """Declare producers and bindings first, then give consumers their keys and manifest arguments."""
    bindings = {}
    if directory is None:
        return bindings
    requests = json.loads((directory / 'elf-requests.json').read_text())
    # Hosted workers stage descriptors separately. A local binding carries its
    # worktree-relative snapshot path so a subsequent Moon run needs no CI env.
    location = ['--targets', str(directory.relative_to(ROOT))] if local else []
    for key, request in requests.items():
        suite, kind = request['suite'], request['kind']
        archive = archive_id(key)
        inputs = [*group(*recipe(suite)), 'tools/ci/archives.py', 'sims/program-test/archive.py',
                  'sw/build/program_target.py']
        if kind == 'harness':
            inputs.append('tools/ci/harness.py')
        elif kind == 'native':
            inputs.append('tools/ci/programs.py')
        tarball = str(ARCHIVES / (key + '.tar.gz'))
        builder = 'software-' + suite
        env = dict(RHODIUM_ARCHIVE_PLATFORM=platform.system() + '-' + platform.machine(),
                   RHODIUM_ARCHIVE_KEY=key, RHODIUM_ARCHIVE_KIND=kind,
                   RHODIUM_BUILD_SOC=request['targets'][0])
        if local:
            env['RHODIUM_ARCHIVE_TARGETS'] = str(directory.relative_to(ROOT))
        if kind == 'native':
            env.update(native_build_env(suite, request['options']))
        elif kind == 'harness':
            builder = BY_SUITE[suite].builder
            env['RISCV_CC'] = NATIVE_COMPILER
        elif suite == 'litmus':
            builder = 'software-litmus-smoke'
            env['LITMUS_SMOKE_RUNS'] = str(request['options']['runs'])
        elif suite == 'linux':
            env['LINUX_JOBS'] = '4'
        tasks.nodes[archive] = dict(extends=builder,
            deps=[], inputs=inputs, outputs=[tarball], env=env,
            options=dict(cache=kind in ('native', 'harness'), envOverride=True))
        if kind in ('native', 'harness'):
            probe = ['python3', '-m', 'tools.ci.archives', 'fingerprint', key, *location]
            tasks.nodes[archive]['checks'] = [dict(check='fingerprint', script=shlex.join(probe))]
        for soc in request['targets']:
            if kind == 'native':
                bind = f'elf:{soc}:{suite}'
            elif kind == 'linux':
                bind = 'linux:' + soc
            else:
                bind = f'bind:{soc}:{key}'
            output = str(Path('.rhodium-cache/moon/bound') / soc / key / 'run-manifest.json')
            tasks.add(bind, ['python3', '-m', 'tools.ci.archives', 'bind', key, '--soc', soc,
                             '--archive', tarball, '--output', output, *location],
                      deps=[archive, 'target:' + soc], inputs=['sw/build/bind.py'], hosted=kind == 'linux')
            bindings[kind, soc, suite] = (bind, output)
    return bindings


def hardware_tasks(add, bindings):
    smoke_variants = {(soc, backend) for soc, _, _ in BACKEND_SMOKE_CONFIGS for backend in BACKEND_SMOKE_VARIANTS}
    for soc, (shape, core, isa) in SELECTIONS.items():
        config = configuration_inputs(soc)
        target = add('target:' + soc, extends='emit-program-target', env=dict(SOC=soc),
                     inputs=(*NATIVE_WRITER_INPUTS, *config), deps=('racket-cache',),
                     outputs=[f'.rhodium-cache/program-targets/{soc}.json'])
        for backend in (DEFAULT_BACKEND, *BACKEND_SMOKE_VARIANTS):
            enrolled = soc in TEST_CONFIGS if backend == DEFAULT_BACKEND else (soc, backend) in smoke_variants
            model_tasks(add, soc, shape, core, backend)
            selected_variant = simulator_options(soc)['variant']
            sim = add(f'simulator:{soc}:{backend}',
                      extends='publish-simulator', env=execution_env(soc, backend),
                      deps=(f'model:{soc}:{backend}:{selected_variant}',
                      target), inputs=('.github/workflows/ci-simulator.yml', *group('simulator-artifact')), hosted=enrolled)
            add(f'run:{soc}:{backend}', extends='simulator-run',
                env=execution_env(soc, backend), deps=[sim])
            offered = (SOFTWARE_TESTS.get((shape, isa), ()) if backend == DEFAULT_BACKEND
                       else BACKEND_SMOKE_VARIANTS[backend]['software_tests'])
            for test in offered:
                harness_task(add, bindings, soc, backend, sim, test, enrolled)


def model_tasks(add, soc, shape, core, backend):
    files, base_groups = emission_inputs(shape, core, backend)
    trace_modes = (False, True) if backend == 'circt' and core != 'spike' else (False,)
    cosim_modes = (False, True) if shape in ('mini', 'simple') and core != 'spike' else (False,)
    for trace, cosim, stats in product(trace_modes, cosim_modes, (False, True)):
        variant = model_variant(trace=trace, cosim=cosim, stats=stats)
        files, groups = emission_inputs(shape, core, backend, cosim=cosim, stats=stats)
        env = dict(SOC=soc, RTL_BACKEND=backend, TRACE=str(int(trace)), COSIM=str(int(cosim)), STATS=str(int(stats)))
        suffix = ('' if backend == 'rsim' else '-' + backend) + ('' if variant == 'normal' else '-' + variant.removeprefix('normal-'))
        directory = f'.rhodium-cache/moon/models/{soc}{suffix}'
        key = f'{soc}:{backend}:{variant}'
        emitted = ['soc_harness.mlir' if backend == 'circt' else 'SoCHarness.sv']
        if backend == 'rsim':
            emitted += ['SoCHarness.cpp', 'SoCHarness_bridge.cpp', 'SoCHarness.hpp', 'rsim-bits.hpp']
        if trace:
            emitted += ['soc_events.h', 'soc_events.json']
        if cosim:
            emitted += ['soc_cosim.json']
        if stats:
            emitted += ['soc_statistics.h', 'soc_statistics.json']
        rtl = add('emit:' + key, extends='emit-model', env=env,
                  inputs=[*files, *group(*groups)], deps=['racket-cache'],
                  outputs=[directory + '/' + f for f in emitted])
        if backend == 'circt':
            rtl = add('lower:' + key, extends='lower-model', env=env, deps=[rtl],
                      outputs=[directory + '/SoCHarness.sv'])
        deps = [rtl]
        if cosim:
            deps += ['build-cosim-runtime', add('cosim-config:' + key, extends='configure-model-cosim', env=env,
                     deps=['emit:' + key, 'build-sail'], inputs=configuration_inputs(soc),
                     outputs=[directory + '/cosim'])]
        if trace:
            deps.append('trace-runtime')
        add('model:' + key, extends='compile-model', env=env, inputs=group(*RUNTIME), deps=deps,
            outputs=[directory + '/VTestDriver'])
        add('publish:' + key, extends='publish-simulator', env=env,
            deps=['model:' + key, 'target:' + soc])


def harness_task(add, bindings, soc, backend, sim, test, enrolled):
    deps, inputs = [sim], group('compiler')
    env = execution_env(soc, backend)
    base = 'harness-' + test
    if test in BY_TEST:
        shared = BY_TEST[test]
        inputs += ['tools/ci/harness.py', *group(*recipe(shared.suite), *RUNNER)]
        if shared.performance:
            inputs.append(baseline(soc, shared.suite))
        binding = bindings.get(('harness', soc, shared.suite))
        base = 'native-program-run'
        env.update(program_env(shared.subdir, shared.suite))
        if shared.performance:
            env.update(RHODIUM_TIMEOUT_VAR='TILED_MT_BENCHMARK_TIMEOUT', RHODIUM_TIMEOUT='600')
        if binding:
            bind, manifest = binding
            deps.append(bind)
            env['RHODIUM_MANIFEST'] = manifest
        else:
            deps.append(add(f'harness-elf:{soc}:{backend}:{test}', extends=shared.builder,
                            env=dict(RHODIUM_BUILD_SOC=soc, RISCV_CC=NATIVE_COMPILER),
                            inputs=inputs, deps=['target:' + soc]))
    else:
        program = {'boot-test': 'smoke', 'smoke': 'smoke'}.get(test, test.removesuffix('-test').replace('-', '_'))
        inputs.extend(('sims/tests/programs/' + program + '.*', 'sims/tests/programs/*.h'))
        if test == 'uart-pty-test':
            inputs.extend(('sims/tests/uart_pty_test.py', 'sims/tests/programs/smoke.ld'))
        env['RHODIUM_HARNESS'] = test
        deps.extend(payload_tasks(add, soc, backend, test, env))
    add(f'harness:{soc}:{backend}:{test}', extends=base, env=env,
        inputs=inputs, deps=deps, hosted=enrolled)


def payload_tasks(add, soc, backend, test, env):
    names = {'smoke': ['smoke'], 'boot-test': ['boot_2000', 'boot_3000'],
             'lrsc-test': ['lrsc_0', 'lrsc_1'], 'cosim-smoke': ['cosim-smoke']}.get(
                 test, [test.removesuffix('-test').replace('-', '_')])
    variant = model_variant(trace=env.get('TRACE') == '1', cosim=env.get('COSIM') == '1', stats=env.get('STATS') == '1')
    result = []
    for name in names:
        source = ('smoke' if name.startswith('boot_') else 'lrsc' if name.startswith('lrsc_') else name)
        inputs = ['sims/tests/programs/' + source + '.*', 'sims/tests/programs/smoke.ld']
        if name == 'cosim-smoke':
            inputs.append('sims/cosim/tests/runtime/scalar.S')
        result.append(add(f'payload:{soc}:{backend}:{variant}:{name}', extends='compile-payload',
                          env={**env, 'RHODIUM_PAYLOAD': name}, inputs=inputs,
                          outputs=[f'.rhodium-cache/moon/payloads/{soc}/{variant}/{name}.elf']))
    return result


def execution_env(soc, backend=DEFAULT_BACKEND):
    """Bind execution to the same explicit instrumentation as its simulator producer."""
    options = simulator_options(soc)
    return dict(RHODIUM_SOC=soc, RHODIUM_BACKEND=backend, RHODIUM_VARIANT=options['variant'],
                SOC=soc, RTL_BACKEND=backend, TRACE='0',
                COSIM=str(int(options['cosim'])), STATS=str(int(options['stats'])))


def program_env(directory, family):
    """Keep each suite's execution bounds next to its Moon task family."""
    timeout = {'bringup': 600, 'skl': 900, 'highway': 900, 'litmus': 600,
               'opensbi': 1800, 'linux': 1800}.get(family, 300)
    timeout_var = (family.upper() + '_TIMEOUT' if family in
                   ('bringup', 'skl', 'highway', 'opensbi', 'linux') else 'PROGRAM_TIMEOUT')
    cycles = {'isa': 10000000, 'litmus': 2000000, 'opensbi': 50000000,
              'linux': 500000000}.get(family, 100000000)
    cycles_var = 'PROGRAM_MAX_CYCLES' if family == 'isa' else family.upper() + '_MAX_CYCLES'
    if family == 'litmus':
        timeout_var = 'LITMUS_FULL_TIMEOUT' if directory == 'litmus-full' else 'LITMUS_SMOKE_TIMEOUT'
        cycles_var = 'LITMUS_PROFILE_MAX_CYCLES'
    return dict(RHODIUM_SUITE_DIR=directory, RHODIUM_TIMEOUT=str(timeout),
                RHODIUM_TIMEOUT_VAR=timeout_var, RHODIUM_MAX_CYCLES=str(cycles),
                RHODIUM_CYCLES_VAR=cycles_var)


def workload_tasks(add, bindings):
    hosted_programs = set(native_programs())
    for soc, suite in native_programs(local=True):
        family = 'coremark' if suite == 'coremark_scalar' else suite
        hosted = (soc, suite) in hosted_programs
        count = PROGRAM_SHARD_COUNTS.get(suite, 1) if hosted else 1
        binding = bindings.get(('native', soc, suite))
        if binding:
            elf, manifest = binding
            suffix = {'RHODIUM_MANIFEST': manifest}
        else:
            elf = add(f'elf:{soc}:{suite}', extends='software-' + suite,
                      env=dict(RHODIUM_BUILD_SOC=soc, **native_build_env(suite, build_options(suite))),
                      inputs=[*group(*recipe(family)), 'tools/ci/programs.py'], deps=['target:' + soc])
            suffix = {}
        add(f'program:{soc}:{suite}', extends='native-program-run',
            env={**execution_env(soc), **program_env(suite, family), **suffix},
            inputs=(baseline(soc, suite), '.github/workflows/ci-program-builds.yml',
                    '.github/workflows/ci-software.yml', *group(*RUNNER)),
            deps=(f'simulator:{soc}:{DEFAULT_BACKEND}', elf), hosted=hosted,
            shards=(dict(RHODIUM_SHARD_COUNT=str(count), RHODIUM_SHARD_INDEX=str(n)) for n in range(count)))

    hosted_platforms = {(r['soc'], r['suite']) for r in platform_configs()}
    for row in platform_configs(local=True):
        soc, suite = row['soc'], row['suite']
        sim = f'simulator:{soc}:{DEFAULT_BACKEND}'
        binding = bindings.get(('platform', soc, suite))
        deps = [sim]
        env = {**execution_env(soc), **program_env('litmus-smoke' if suite == 'litmus' else suite, suite)}
        if binding:
            bind, manifest = binding
            env['RHODIUM_MANIFEST'] = manifest
            deps.append(bind)
        else:
            deps.append(add(f'platform-elf:{soc}:{suite}',
                            extends='software-' + ('litmus-smoke' if suite == 'litmus' else 'opensbi'),
                            env=dict(RHODIUM_BUILD_SOC=soc),
                            inputs=group(*recipe(suite)), deps=['target:' + soc]))
        add(f'platform:{soc}:{suite}', extends='native-program-run', env=env,
            inputs=('.github/workflows/ci-platform*.yml', *group(*recipe(suite), *RUNNER)),
            deps=deps, hosted=(soc, suite) in hosted_platforms)
        if suite == 'litmus':
            elf = add('litmus-full-elf:' + soc, extends='software-litmus-full', env=dict(RHODIUM_BUILD_SOC=soc),
                      inputs=group(*recipe(suite)), deps=['target:' + soc])
            add('litmus-full:' + soc, extends='native-program-run',
                env={**execution_env(soc), **program_env('litmus-full', 'litmus')},
                inputs=group(*recipe(suite), *RUNNER), deps=[sim, elf])
    for row in linux_configs():
        soc = row['soc']
        binding = bindings.get(('linux', soc, 'linux'))
        if not binding:
            add('linux:' + soc, extends='software-linux', env=dict(RHODIUM_BUILD_SOC=soc),
                inputs=('.github/workflows/ci-linux-builds.yml', *group(*recipe('linux'), 'bundle', 'build-groups')),
                deps=['target:' + soc], hosted=True)
        env = {**execution_env(soc), **program_env('linux', 'linux')}
        if binding:
            env['RHODIUM_MANIFEST'] = binding[1]
        add('linux-boot:' + soc, extends='native-program-run', env=env,
            deps=['linux:' + soc, f'simulator:{soc}:{DEFAULT_BACKEND}'])


def act_tasks(add):
    hosted_configs = {r['configuration'] for r in arch_configs()}
    for row in arch_configs(local=True):
        soc = row['configuration']
        config = add('act-config:' + soc, extends='configure-act', env=dict(ACT_CONFIGURATION=soc),
                     inputs=('.moon/tasks/act-build.yml', 'sims/arch-test/write-platform.rhm',
                             'sims/arch-test/platform.rhm', 'sims/arch-test/configure.py',
                             *group(*ACT_TOOLS, 'compiler'), *configuration_inputs(soc)),
                     deps=['racket-cache', 'build-sail'])
        deps = [config, 'act-generate']
        if soc == 'simple-rv5stage-rva23':
            deps.append('sail-alias-check')
        elfs = add('act-elf:' + soc, extends='compile-act', env=dict(ACT_CONFIGURATION=soc),
                   inputs=group('act-source'), deps=deps)
        # Test the model and ACT platform, not the native descriptor writer; consume an attested simulator.
        hosted = soc in hosted_configs
        count = ARCH_SHARD_COUNTS[soc] if hosted else 1
        add('act:' + soc, extends='arch-program-run', env=execution_env(soc),
            inputs=('.github/workflows/ci-arch*.yml', '.github/workflows/ci-simulator.yml', *group('evidence', 'simulator-artifact')),
            deps=[elfs, f'simulator:{soc}:{DEFAULT_BACKEND}'], hosted=hosted,
            shards=(dict(RHODIUM_SHARD_COUNT=str(count), RHODIUM_SHARD_INDEX=str(n)) for n in range(count)))


def local_execution_tasks(add):
    """Expose parameterized local execution without changing the hosted selections."""
    target = add('local-target', ['bash', 'tools/build/local.sh', 'target'], inputs=['tools/build/local.sh'])
    sim = add('local-simulator', ['bash', 'tools/build/local.sh', 'simulator'], inputs=['tools/build/local.sh'])
    add('sim-run', extends='simulator-run', deps=[sim])
    for test in ('smoke', 'boot-test', 'uart-pty-test', 'host-mmio-test', 'zicboz-test',
                 'zihintntl-test', 'lrsc-test', 'cosim-smoke'):
        flags = {'COSIM': '1'} if test == 'cosim-smoke' else {'TRACE': '0'} if test == 'lrsc-test' else {}
        names = {'boot-test': ['boot_2000', 'boot_3000'], 'lrsc-test': ['lrsc_0', 'lrsc_1'],
                 'cosim-smoke': ['cosim-smoke']}.get(test, [test.removesuffix('-test').replace('-', '_')])
        payloads = []
        for name in names:
            payloads.append(add('local-payload:' + name, ['bash', 'sims/build-payload.sh'],
                            env={'RHODIUM_PAYLOAD': name},
                            inputs=['sims/build-payload.sh', 'sims/tests/programs/*', 'sims/cosim/tests/runtime/scalar.S']))
        add('sim-' + test, extends='harness-' + test, env={'RHODIUM_HARNESS': test, **flags},
            deps=[*[dict(target=task_id(p), env=flags) for p in payloads], dict(target=sim, env=flags)] if flags else [*payloads, sim])
    # Local aliases retain caller-selected build options; configured products
    # use the complete canonical recipe above.
    suites = [(s, s, s) for s in
              ('isa', 'benchmark', 'coremark', 'coremark_scalar', 'embench', 'bringup', 'skl', 'highway')]
    suites += [('isa-smoke', 'isa-smoke', 'isa'),
               ('tiled-mt-benchmark', 'mt-benchmark/8-harts', 'benchmark'),
               ('litmus', 'litmus', 'litmus'),
               ('litmus-smoke', 'litmus-smoke', 'litmus'),
               ('litmus-full', 'litmus-full', 'litmus'),
               ('opensbi', 'opensbi', 'opensbi'),
               ('linux', 'linux', 'linux')]
    for key, directory, family in suites:
        elf = add(key + ('-elf' if key.startswith('coremark') else '-elfs'),
                  extends='software-' + key, deps=[target])
        env = program_env(directory, 'coremark' if family == 'coremark_scalar' else family)
        if key == 'tiled-mt-benchmark':
            env.update(RHODIUM_TIMEOUT_VAR='TILED_MT_BENCHMARK_TIMEOUT', RHODIUM_TIMEOUT='600')
        if key == 'litmus':
            env.update(RHODIUM_TIMEOUT_VAR='PROGRAM_TIMEOUT', RHODIUM_TIMEOUT='300',
                       RHODIUM_CYCLES_VAR='LITMUS_MAX_CYCLES', RHODIUM_MAX_CYCLES='20000000')
        add('sim-' + key + '-test', extends='native-program-run', env=env, deps=[sim, elf])
        run = 'sim-opensbi-run-suite' if key == 'opensbi' else 'sim-' + key + '-run'
        add(run, extends='native-program-run', env=env, deps=[sim])
    config = add('local-act-elfs', ['bash', 'tools/build/local.sh', 'act'], inputs=['tools/build/local.sh'])
    add('sim-arch-test', extends='arch-program-run', deps=[sim, config])
    add('sim-arch-test-run', extends='arch-program-run', deps=[sim])


def materialize(directory=None, paths=(), *, local=False):
    tasks = Tasks()
    bindings = archive_bindings(tasks, directory, local)
    hardware_tasks(tasks.add, bindings)
    workload_tasks(tasks.add, bindings)
    act_tasks(tasks.add)
    local_execution_tasks(tasks.add)
    # Public software tasks share the same layout/firmware/payload graph as CI
    # archives; no Make invocation or hidden build ordering remains in a runner.
    for key, node in list(tasks.nodes.items()):
        if node.get('extends') == 'software-opensbi':
            node['deps'] = [*node.get('deps', []), *[task_id(d) for d in opensbi_stages(
                tasks, key, node.setdefault('env', {}), node.get('deps', []))]]
    for soc, suites in simple_software():
        tasks.add('software:' + soc, ['noop'], deps=[*(f'program:{soc}:{s}' for s in suites),
                                                      f'act:{soc}', f'platform:{soc}:opensbi'])
    for key, names in RECIPE_CACHE_KEYS.items():
        tasks.nodes[task_id('hash:' + key)] = dict(command='noop', tags=['ci-hash'], inputs=sorted(set(group(*names))))
    groups = catalog_groups(paths)
    write_catalog(tasks.nodes, groups)
    return {name for name in groups if name.startswith('fixtures--')}


def write_catalog(tasks, groups):
    contents = '# Expands the current configured moon task families; do not edit.\n# SPDX-License-Identifier: Apache-2.0\n' + json.dumps({'tasks':tasks, 'fileGroups':groups}, sort_keys=True)
    if CATALOG.exists() and CATALOG.read_text() == contents:
        return
    with tempfile.NamedTemporaryFile(dir=CATALOG.parent, mode='w', delete=False) as f:
        f.write(contents)
    os.replace(f.name, CATALOG)


def main():
    parser = argparse.ArgumentParser(description='Generate Moon configuration without executing build tasks.')
    parser.add_argument('--soc', action='append', metavar='SOC', choices=[s for s, _, _ in software_configs(True)],
                        help='bind local workloads using descriptors already built by rhodium:target--SOC (repeatable)')
    args = parser.parse_args()
    directory = os.environ.get('PROGRAM_TARGETS')
    if args.soc:
        if directory:
            parser.error('use --soc for local resolution or PROGRAM_TARGETS for staged descriptors, not both')
        from .archives import local_catalog
        local_catalog(set(args.soc))
    else:
        materialize(Path(directory) if directory else None)


if __name__ == '__main__':
    main()
