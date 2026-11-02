# Declares software input dependencies for workload selection and suite-local CI build caches.
# SPDX-License-Identifier: Apache-2.0

from .graph import Product


NATIVE_SUITES = ('isa', 'benchmark', 'coremark', 'embench', 'bringup', 'skl', 'highway')


# Edges point from a consumer to its inputs. Build roots intentionally exclude
# execution contracts, adapter tests, and performance expectations.
INPUTS = {
    'compiler': Product(('tools/install-riscv-*.sh', 'riscv/gnu-toolchain.rhm',
                        '.github/actions/setup-riscv-toolchain/*')),
    'elf': Product(('sw/build/build.py', 'sw/build/program_target.py')),
    'patches': Product(('riscv/patched_submodule.py',)),
    'build-driver': Product(('sims/program-test/Makefile.inc',)),
    'isa-source': Product(('sw/riscv-isa-tests', 'sw/riscv-isa-tests/*')),
    'bundle': Product(('sw/build/bind.py', 'sims/program-test/archive.py')),
    'build-groups': Product(('tools/ci/programs.py',)),
    'evidence': Product(('sims/program-test/execution.py',)),
    'simulator-artifact': Product(('sims/program-test/artifact.py',)),
    'native-runner': Product(('sims/program-test/run.py', 'sims/program-test/performance.py'),
                            ('bundle', 'evidence', 'simulator-artifact')),
}

BUILD_INPUTS = {
    'isa': Product(('sw/build/isa.mk',), ('compiler', 'elf', 'build-driver', 'isa-source')),
    'benchmark': Product(('sw/riscv-isa-tests-patches/*',),
                        ('compiler', 'elf', 'patches', 'build-driver', 'isa-source')),
    'coremark': Product(('sw/build/build-coremark.py', 'sw/coremark', 'sw/coremark/*',
                       'sw/coremark-riscv-baremetal/*'), ('compiler', 'elf', 'build-driver')),
    'embench': Product(('sw/build/build-embench.py', 'sw/embench-iot', 'sw/embench-iot/*',
                      'sw/embench-iot-patches/*', 'sw/embench-iot-riscv-baremetal/*'),
                     ('compiler', 'elf', 'patches', 'build-driver')),
    'bringup': Product(('sw/build/build-bringup-bench.py', 'sw/bringup-bench', 'sw/bringup-bench/*',
                      'sw/bringup-bench-patches/*', 'sw/bringup-bench-riscv-baremetal/*'),
                     ('compiler', 'elf', 'patches', 'build-driver')),
    'skl': Product(('sw/build/build-skl.py', 'sw/skl', 'sw/skl/*', 'sw/skl-patches/*',
                  'sw/skl-riscv-baremetal/*'), ('compiler', 'elf', 'patches', 'build-driver')),
    'highway': Product(('sw/build/build-highway.py', 'sw/highway', 'sw/highway/*',
                      'sw/highway-riscv-baremetal/*'), ('compiler', 'elf', 'build-driver')),
    'litmus': Product(('sw/build/build-litmus.py', 'sw/litmus-tests-riscv', 'sw/litmus-tests-riscv/*',
                     'sw/litmus-riscv-baremetal/*'),
                    ('compiler', 'elf', 'patches', 'build-driver', 'isa-source')),
    'opensbi': Product(('sw/build/opensbi.py', 'sw/opensbi', 'sw/opensbi/*',
                      'sw/opensbi-patches/*', 'sims/opensbi/*'),
                     ('compiler', 'elf', 'patches')),
    'linux': Product(('sw/build/linux.py', 'sw/buildroot', 'sw/buildroot/*',
                    'sw/buildroot-riscv/*', 'sims/linux/*'), ('build:opensbi',)),
}
INPUTS.update(('build:' + suite, inputs) for suite, inputs in BUILD_INPUTS.items())
INPUTS['build:coremark_scalar'] = Product(requires=('build:coremark',))
for suite in NATIVE_SUITES:
    INPUTS['native:' + suite] = Product(('.github/workflows/ci-software.yml',),
                                       ('build:' + suite, 'native-runner', 'build-groups'))
for suite in ('litmus', 'opensbi'):
    INPUTS['platform:' + suite] = Product(requires=('build:' + suite, 'native-runner', 'build-groups'))
INPUTS['image:linux'] = Product(requires=('build:linux', 'bundle', 'build-groups'))
INPUTS['arch:act'] = Product(requires=('evidence', 'simulator-artifact'))
INPUTS['simulator:all'] = Product(requires=('simulator-artifact',))
for suite in ('isa', 'benchmark'):
    INPUTS['harness:' + suite] = Product(requires=('build:' + suite, 'native-runner', 'build-groups'))

# Adapter-only edits run host checks, not guest workloads. Setup belongs to the
# actual checked sources, not to the simulator matrix.
ADAPTERS = {
    'program': ('test_program_build.py', (), ('elf', 'native-runner')),
    'litmus': ('test_litmus_build.py', ('litmus-setup',), ('build:litmus',)),
    'opensbi': ('test_opensbi_build.py', (), ('build:opensbi',)),
    'linux': ('test_linux_target.py', (), ('build:linux',)),
    'inputs': ('test_benchmark_inputs.py', ('bringup-setup', 'embench-setup', 'skl-setup'),
               ('build:bringup', 'build:embench', 'build:skl', 'build:highway')),
}
for name, (filename, _setup, dependencies) in ADAPTERS.items():
    INPUTS['adapter:' + name] = Product(('sw/tests/' + filename,), dependencies)
