# Declares repository build products and their consumers; CI enrollment is separate from availability.
# SPDX-License-Identifier: Apache-2.0

from .graph import Graph
from .policy import (BACKEND_SMOKE_CONFIGS, BACKEND_SMOKE_VARIANTS, CHECKS, DEFAULT_BACKEND,
                     NATIVE_SUITES, ROOT, SELECTIONS, SOFTWARE_TESTS, TEST_CONFIGS, arch_configs,
                     linux_configs, model_variant, native_configs, platform_configs, simulator_options)
from .software import INPUTS


def build_graph():
    graph = Graph()
    add = graph.add
    for key, spec in INPUTS.items():
        add(key, inputs=spec.inputs, requires=spec.requires, ci=key.startswith('adapter:'))

    tests = ('*/tests/*', '*/check-boundaries.sh')
    def source(key, *patterns, exclude=(), requires=()):
        return add(key, inputs=patterns, excludes=(*tests, *exclude), requires=requires)

    # Operational prerequisites (bytecode, toolchains) are distinct from an
    # individual module's semantic inputs. Recompiling a module is not a reason
    # to rerun every test that uses the Racket installation.
    add('racket-installer', inputs=('tools/ci/install-racket.sh', '.github/actions/setup-racket/*'))
    add('bytecode', inputs=('tools/*racket*.sh', 'tools/invalidate-racket-build-cache.rkt',
                            '.github/actions/setup-racket*/*'), requires=('racket-installer',))
    source('language', 'rhodium/core/*', 'rhodium/base/*', 'rhodium/lowering/*',
           'rhodium/analysis/*', 'rhodium/frontend/*', 'rhodium/main.rkt', 'rhodium/language.rhm')
    source('library', 'rhodium/std/*', 'flow/*', 'support/*')
    source('compilation', 'rhodium/compile/*', 'rhodium/event/*', 'rhodium/diagram/*')
    add('circt-tool', inputs=('tools/install-circt.sh', '.github/actions/setup-circt/*'))
    for backend, files in {'circt': ('circt*',), 'verilog': ('verilog*',),
                           'rsim': ('rsim*', 'verilog*')}.items():
        source('backend:' + backend, *('rhodium/backend/' + f for f in files),
               requires=('circt-tool',) if backend == 'circt' else ())
    source('architecture', 'riscv/isa/*', 'riscv/model/*', 'riscv/rtl/*', 'riscv/udb*',
           'cores/profile.rhm', 'cores/udb.rhm', 'devicetree/*')
    profiles = {'rv5stage': 'core-profiles.rhm', 'rv2wide': 'rv2wide-core-profile.rhm',
                'rv3wide': 'rv3wide-core-profile.rhm', 'spike': 'spike-core-profile.rhm'}
    source('soc-config', 'socs/configs/*', 'socs/platform/*', 'socs/harts/implementation.rhdl',
           'sims/config.mk', 'sims/test-config*', 'sims/program-test/*target*.rhm',
           exclude=tuple('socs/configs/' + p for p in profiles.values()))
    source('soc-hardware', 'chi/*', 'noc/*', 'devices/*')
    source('hart-interface', 'cores/chi-hart.rhdl', 'cores/cosim-source.rhm')
    source('shared-core', 'cores/*', 'hardfloat/*',
           exclude=('cores/rv2wide/*', 'cores/rv3wide/*', 'cores/superscalar/*', 'cores/rv5stage/*', 'cores/spike/*'))
    source('superscalar', 'cores/superscalar/*')
    for core in sorted({core for _, core, _ in SELECTIONS.values()}):
        source('core:' + core, f'cores/{core}/*', f'socs/harts/{core}.rhdl',
               requires=('superscalar',) if core in ('rv2wide', 'rv3wide') else ())
        source('profile:' + core, 'socs/configs/' + profiles[core], f'cores/{core}/profile.rhm',
               f'cores/{core}/udb.rhm', f'socs/harts/{core}.rhdl',
               *(('cores/vector/profile.rhm', 'cores/fp/profile.rhm', 'cores/cache/config.rhm',
                  'cores/csr/config.rhm') if core != 'spike' else ()),
               *(('cores/superscalar/profile.rhm', 'cores/superscalar/udb.rhm') if core in ('rv2wide', 'rv3wide') else ()))
    source('shape:mini', 'socs/mini-soc/*', 'socs/one-hart/*')
    source('shape:simple', 'socs/single-core-soc/*', 'socs/one-hart/*')
    source('shape:tiled', 'socs/tiled-soc/*')
    add('fesvr-library', inputs=('riscv/riscv-isa-sim', 'riscv/riscv-isa-sim-patches/*',
                                'sims/fesvr/install.sh', 'sims/Makefile', '.github/actions/setup-fesvr/*'),
        requires=('patches',))
    add('fesvr', inputs=('sims/fesvr/*',), requires=('fesvr-library',))
    add('sail', inputs=('sims/sail/*', 'sims/arch-test/install-sail.sh', 'riscv/sail-riscv',
                        'riscv/sail-riscv-patches/*'), requires=('patches',))
    add('cosim', inputs=('sims/cosim/*',), requires=('sail',))
    add('statistics', inputs=('sims/statistics/*', 'rhodium/statistics/*'))
    add('verilator', inputs=('tools/install-verilator.sh', '.github/actions/setup-verilator/*'))
    add('native-toolchain')
    add('harness-runtime', inputs=('sims/Makefile', 'sims/verilator/*', 'sims/TestDriver.v',
                                   'sims/emit-soc-harness.rhm'), requires=('fesvr', 'verilator'))
    for target, paths, deps in (
        ('dpi-compile-check', ('devices/uart/dpi/*', 'chi/subordinate/dpi/*', 'sims/verilator/direct_mem_htif*'),
         ('fesvr', 'verilator', 'native-toolchain')),
        ('transport-test', ('sims/tests/direct*',), ('fesvr', 'bytecode', 'language', 'library', 'native-toolchain')),
        ('arch-test-adapter-test', ('sims/arch-test/*', 'sims/sail/*', 'sims/tests/test_arch_test.py',
                                   'sims/tests/test_sail_config.py'), ()),
        ('cosim-hooks-test', ('sims/cosim/*',), ('native-toolchain',)),
        ('simulation-runtime-test', ('sims/verilator/*', 'sims/tests/*simulation-runtime*', 'sims/tests/*statistics*',
                                    'sims/emit-soc-harness.rhm'),
         ('statistics', 'bytecode', 'language', 'library', 'compilation', 'backend:rsim', 'backend:verilog',
          'native-toolchain', 'verilator')),
        ('branch-prediction-test', ('sims/branch-prediction.py', 'sims/tests/test_branch_prediction.py'), ()),
        ('spike-core-test', ('cores/spike/tests/*.cc',), ('core:spike', 'fesvr', 'native-toolchain')),
        ('spike-dpi-test', ('cores/spike/tests/*.cc',), ('core:spike', 'fesvr', 'native-toolchain', 'verilator')),
        ('spike-dpi-abi-check', ('cores/spike/tests/emit.rhm',), ('core:spike', 'fesvr', 'backend:circt',
                                   'language', 'library', 'bytecode', 'native-toolchain', 'verilator')),
    ):
        add('native-check:' + target, inputs=(*paths, 'sims/Makefile'), requires=deps, ci=True)

    smoke_variants = {(soc, backend) for soc, _, _ in BACKEND_SMOKE_CONFIGS for backend in BACKEND_SMOKE_VARIANTS}
    for soc, (shape, core, isa) in SELECTIONS.items():
        shape_family = 'tiled' if shape.startswith('tiled') else shape
        target = add('target:' + soc, requires=('bytecode', 'language', 'library', 'architecture',
                    'soc-config', 'soc-hardware', 'hart-interface', 'shape:' + shape_family, 'profile:' + core))
        harness = {'mini': 'mini', 'simple': 'single-core', 'tiled': 'tiled'}[shape_family]
        design = add('design:' + soc, inputs=(f'sims/{harness}-soc-harness.rhdl',),
                     requires=(target, 'core:' + core,
                               *(('shared-core',) if core != 'spike' else ())))
        for backend in (DEFAULT_BACKEND, *BACKEND_SMOKE_VARIANTS):
            enrolled = soc in TEST_CONFIGS if backend == DEFAULT_BACKEND else (soc, backend) in smoke_variants
            for trace in ([False, True] if backend == 'circt' and core != 'spike' else [False]):
                for cosim in ([False, True] if shape in ('mini', 'simple') and core != 'spike' else [False]):
                    for stats in (False, True):
                        variant = model_variant(trace=trace, cosim=cosim, stats=stats)
                        deps = [design, 'compilation', 'backend:' + backend, 'harness-runtime']
                        if cosim:
                            deps.append('cosim')
                        if stats:
                            deps.append('statistics')
                        if trace:
                            deps.append('trace-runtime')
                        add(f'model:{soc}:{backend}:{variant}', requires=deps)
            selected_variant = simulator_options(soc)['variant']
            sim = add(f'simulator:{soc}:{backend}', requires=(f'model:{soc}:{backend}:{selected_variant}',
                      'simulator:all'), inputs=('.github/workflows/ci-simulator.yml',), ci=enrolled)
            offered = (SOFTWARE_TESTS.get((shape, isa), ()) if backend == DEFAULT_BACKEND
                       else BACKEND_SMOKE_VARIANTS[backend]['software_tests'])
            for test in offered:
                deps = [sim, 'compiler']
                paths = []
                if test in ('isa-smoke', 'tiled-mt-benchmark-test'):
                    suite = 'isa' if test == 'isa-smoke' else 'benchmark'
                    deps.append('harness:' + suite)
                    if suite == 'benchmark':
                        paths.append(baseline(soc, suite))
                else:
                    program = {'boot-test': 'smoke', 'smoke': 'smoke'}.get(
                        test, test.removesuffix('-test').replace('-', '_'))
                    paths.extend(('sims/tests/programs/' + program + '.*', 'sims/tests/programs/*.h'))
                    if test == 'uart-pty-test':
                        paths.extend(('sims/tests/uart_pty_test.py', 'sims/tests/programs/smoke.ld'))
                add(f'harness:{soc}:{backend}:{test}', inputs=paths, requires=deps, ci=enrolled)
    for suite in (*NATIVE_SUITES, 'coremark_scalar'):
        family = 'coremark' if suite == 'coremark_scalar' else suite
        for soc in native_configs(family):
            # A target-specific request, not an assertion of ELF identity:
            # programs.py groups requests by the actual generated build spec.
            elf = add(f'elf:{soc}:{suite}', requires=('target:' + soc, 'build:' + family))
            add(f'program:{soc}:{suite}', inputs=(baseline(soc, suite), '.github/workflows/ci-program-builds.yml'),
                requires=(f'simulator:{soc}:{DEFAULT_BACKEND}', elf, 'native:' + family), ci=True)
    for row in platform_configs():
        soc, suite = row['soc'], row['suite']
        add(f'platform:{soc}:{suite}', inputs=('.github/workflows/ci-platform*.yml',),
            requires=(f'simulator:{soc}:{DEFAULT_BACKEND}', 'platform:' + suite), ci=True)
    for row in linux_configs():
        soc = row['soc']
        add('linux:' + soc, inputs=('.github/workflows/ci-linux-builds.yml',),
            requires=('target:' + soc, 'image:linux'), ci=True)
        add('linux-boot:' + soc, requires=('linux:' + soc, f'simulator:{soc}:{DEFAULT_BACKEND}'))
    for row in platform_configs():
        if row['suite'] == 'litmus':
            soc = row['soc']
            add('litmus-full:' + soc, requires=(f'simulator:{soc}:{DEFAULT_BACKEND}', 'platform:litmus'))
    add('act-tools', inputs=('sims/arch-test/setup.sh', 'sw/riscv-arch-test', 'sw/riscv-arch-test-patches/*'),
        requires=('patches',))
    add('act-source', inputs=('sims/arch-test/*', 'tools/write-riscv-udb-config.rhm'),
        requires=('act-tools', 'sail', 'compiler'))
    for row in arch_configs():
        soc = row['configuration']
        elfs = add('act-elf:' + soc, requires=('target:' + soc, 'act-source'))
        add('act:' + soc, inputs=('.github/workflows/ci-arch*.yml',),
            requires=(elfs, 'arch:act', f'simulator:{soc}:{DEFAULT_BACKEND}'), ci=True)
    local_flows(graph)
    checks(graph)
    return graph.finish()


def baseline(soc, suite):
    return f'sims/program-test/performance-baselines/{soc}/{suite}.json'


def local_flows(graph):
    add = graph.add
    # Physical and authoring flows are available locally without enrolling
    # PDK, formal, or editor toolchains on ordinary CI runners.
    add('formal', inputs=('rhodium/formal/*', 'examples/formal/*'), requires=('language', 'library'))
    add('formal-differential', inputs=('rhodium/formal/tests/*',), requires=('formal', 'backend:circt', 'verilator'))
    add('trace-runtime', inputs=('rheg/*', 'sims/tests/*trace*', 'sims/tests/*.sql'),
        requires=('compilation',))
    add('emacs', inputs=('tools/emacs/*',))
    add('diagram:rv5stage', inputs=('tools/write-rv5stage-core-diagram.rhm', 'examples/rv5stage/*'),
        requires=('language', 'library', 'compilation', 'core:rv5stage', 'shared-core'))
    add('diagram:noc', inputs=('tools/write-noc-router-diagram.rhm', 'examples/noc/*'),
        requires=('language', 'library', 'compilation', 'soc-hardware'))
    add('sram', inputs=('sram/*',), requires=('backend:circt',))
    add('memory-map', inputs=('vlsi/designs/mini-rv5stage-soc/*', 'vlsi/tools/check-mini-rv5stage-soc-memory-map.py',
                              'vlsi/tools/emit-mini-rv5stage-soc.rhm', 'vlsi/Makefile', 'sims/mini-rv5stage-soc-harness.rhdl'),
        requires=('sram', 'design:mini-rv5stage-rva23', 'compilation'))
    add('mapped-smoke', inputs=('vlsi/sim/*', 'sims/tests/programs/smoke.*', '.github/workflows/ci-simulator.yml'),
        requires=('memory-map', 'harness-runtime', 'compiler'),
        after=('simulator:mini-rv5stage-rva23:rsim',), ci=True)
    add('synthesis', inputs=('vlsi/openlane/mini_rv5stage_soc/*', 'vlsi/tools/*'), requires=('memory-map',))
    add('wrapper-rtl', inputs=('vlsi/src/*', 'vlsi/verilog/*', 'vlsi/tools/emit-top.rhm', 'vlsi/Makefile'), requires=('language', 'library', 'backend:circt'))
    add('wrapper-contract', inputs=('vlsi/tools/check-openframe-contract.py', 'vlsi/double_wide_openframe'),
        requires=('wrapper-rtl',))
    add('harden', inputs=('vlsi/openlane/double_wide_openframe_project_wrapper/*',), requires=('wrapper-contract',))
    add('gds', requires=('harden',))
    add('lvs', inputs=('vlsi/openlane/rhodium_lvs_smoke/*',), requires=('wrapper-contract',))
    add('goldens', inputs=('examples/*', 'tools/testing/circt/*'), requires=('language', 'library', 'compilation', 'backend:circt'))


def checks(graph):
    # Coverage policies can overlap. They trigger checks without turning
    # those checks or their fixtures into inputs to an ELF or simulator.
    systems = ('cores/*', 'riscv/*', 'hardfloat/*')
    protocols = ('chi/*', 'devices/*', 'noc/*')
    shared = ('language', 'library')
    fixtures = {}
    for line in (ROOT / 'tools/testing/rsim/fixtures.tsv').read_text().splitlines():
        if line and not line.startswith('#'):
            _, group, src, _, driver, *opts = line.split('\t')
            group = {'language': 'std'}.get(group, group.replace('cores-', 'core-'))
            fixtures.setdefault('rsim-' + group, set()).update([src, driver, *(opts[1].split() if len(opts) > 1 else [])])
    for line in (ROOT / 'tools/testing/circt/fixtures.txt').read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        group, kind, fixture, top, *fields = line.split('|')
        key = 'circt-' + group.replace('cores-', 'core-')
        paths = fixtures.setdefault(key, set())
        if kind == 'direct':
            paths.add(f'*/tests/circt/emit-{fixture}.rhm')
        elif kind == 'example':
            paths.add(fields[0])
        else:
            raise ValueError(f'unknown CIRCT fixture kind: {kind}')
        if top:
            paths.update((f'*/tests/circt/verilog/{fixture}_tb.sv', f'*/tests/circt/verilog/{fixture}_dpi.cpp'))
    unowned = fixtures.keys() - {c.key for c in CHECKS}
    if unowned:
        raise ValueError(f'fixture groups without CI owners: {sorted(unowned)}')
    host = {
        'hygiene': ('*.rhdl', '*.rhm', '*.rkt', '*/check-boundaries.sh', 'tools/check-*', 'tools/parameter-*',
                    '.githooks/*', 'examples/*'),
        'foundation': ('support/*', 'rhodium/core/tests/*', 'rhodium/lowering/tests/*',
                       'rhodium/frontend/tests/*', 'rhodium/std/tests/*', 'flow/tests/*',
                       'rhodium/analysis/*', 'rhodium/statistics/*', 'rhodium/event/*',
                       'rhodium/diagram/*', 'rheg/*', 'examples/lop/*', 'examples/formal/*'),
        'backend': ('rhodium/backend/*', 'rhodium/compile/*', 'rhodium/event/*', 'rheg/*', 'devices/uart/*',
                    'devices/tests/*uart*', 'riscv/riscv-isa-sim*', 'riscv/patched_submodule.py', 'examples/lop/*'),
        'models': ('riscv/*', 'devicetree/*', 'hardfloat/*', 'noc/*', 'tools/write-riscv-udb-config.rhm'),
        'protocols': (*protocols, 'rfpl/*'),
        'cores': (*systems, 'tools/write-riscv-udb-config.rhm'),
        'socs': (*systems, *protocols, 'socs/*', 'sims/*/*.rhm', 'sims/cosim/*', 'devicetree/*',
                 'tools/write-riscv-udb-config.rhm'),
        'examples': ('examples/*', *systems, *protocols, 'rfpl/*', 'tools/write-*-diagram.rhm', 'tools/check-example-verilog.sh'),
    }
    circt = {
        'language': ('examples/rtl/*', 'examples/clocking/*', 'examples/lop/*', 'rhodium/formal/*',
                     'rhodium/event/*', 'rheg/*'),
        'std': ('examples/std/*',), 'protocols': (*protocols, 'examples/chi/*', 'examples/noc/*', 'sims/fesvr/*'),
        'rfpl': ('rfpl/*', 'examples/rfpl/*'),
        'core-components': (*systems, 'examples/riscv/*', 'examples/cores/*', 'socs/*', 'sims/cosim/*'),
        'core-execution-datapath': (*systems, 'examples/cores/*', 'socs/*', 'sims/cosim/*'),
    }
    differential = ('rhodium/backend/*', 'rhodium/compile/*', 'devices/uart/*', 'devices/tests/*uart*',
                    'rhodium/event/*', 'rheg/*', 'chi/protocol/*', 'chi/subordinate/*', 'rhodium/statistics/*',
                    'sims/statistics/*', 'examples/std/sync-ram.rhdl', 'sims/TestDriver.v', 'sims/verilator/simulation_runtime.*')
    for check in CHECKS:
        family, _, name = check.key.partition('-')
        deps = ['bytecode', *shared]
        if family == 'host':
            paths = (*host[name], 'tools/testing/run-negative.rkt', 'tools/run-racket-tests.sh')
        elif family == 'rsim':
            owned = fixtures.get(check.key, ())
            paths = ('tools/testing/rsim/*', 'rhodium/backend/*', 'rhodium/compile/*', *owned)
            if name.startswith('core-'):
                deps += ['shared-core', 'architecture']
                # A named implementation selects the groups that exercise it.
                # Shared harts/units retain conservative cross-core coverage.
                for c in ('rv2wide', 'rv3wide', 'rv5stage', 'spike'):
                    if any(p.startswith('cores/' + c + '/') for p in owned):
                        deps.append('core:' + c)
                        paths += (f'cores/{c}/tests/*',)
                if any(p.startswith('cores/superscalar/') for p in owned):
                    deps += ['superscalar', 'core:rv2wide', 'core:rv3wide']
                    paths += ('cores/superscalar/tests/*',)
                paths += ('cores/tests/*', 'riscv/*', 'examples/cores/*', 'examples/riscv/*', 'chi/*', 'rheg/*', 'rhodium/event/*',
                          'rhodium/diagram/*', 'socs/*')
            elif name == 'protocols':
                paths += (*protocols, 'socs/*', 'examples/chi/*', 'examples/noc/*', 'sims/tests/rsim/*',
                          'sims/tests/fesvr-mmio-fixture.rhdl', 'rheg/*', 'rhodium/event/*', 'rhodium/diagram/*')
            elif name == 'hardfloat':
                paths += ('hardfloat/*',)
            elif name == 'std':
                paths += ('examples/std/*', 'examples/rtl/*', 'examples/clocking/*',
                          'rhodium/diagram/*', 'rhodium/event/*', 'rhodium/statistics/*', 'sims/statistics/*', 'rheg/*')
            else:
                raise ValueError(check.key)
        elif family in ('verilog', 'circt'):
            paths = ('tools/check-example-verilog.sh', 'tools/testing/circt/*', 'rhodium/backend/*', 'rhodium/compile/*')
            if family == 'verilog' or name == 'verilog-differential':
                paths += differential
            else:
                paths += (*circt[name], *fixtures.get(check.key, ()))
                if name in ('language', 'std', 'protocols'):
                    paths += differential
            deps += ['backend:circt'] if family == 'circt' else ['backend:verilog']
        else:
            raise ValueError(check.key)
        if check.verilator:
            deps.append('verilator')
        graph.add('check:' + check.key, requires=deps, covers=(*paths, '.github/workflows/ci-checks.yml'), ci=True)
