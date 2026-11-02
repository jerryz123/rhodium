#!/usr/bin/env python3
# Declares software input dependencies for workload selection and suite-local CI build caches.
# SPDX-License-Identifier: Apache-2.0

import argparse
from dataclasses import dataclass
import fnmatch
import hashlib
from pathlib import Path
import subprocess


NATIVE_SUITES = ('isa', 'benchmark', 'coremark', 'embench', 'bringup', 'skl', 'highway')


@dataclass(frozen=True)
class Inputs:
    paths: tuple[str, ...] = ()
    requires: tuple[str, ...] = ()


# Edges point from a consumer to its inputs. Build roots intentionally exclude
# execution contracts, adapter tests, and performance expectations.
INPUTS = {
    'compiler': Inputs(('tools/install-riscv-*.sh', 'riscv/gnu-toolchain.rhm',
                        '.github/actions/setup-riscv-toolchain/*')),
    'elf': Inputs(('sw/build/build.py', 'sw/build/program_target.py')),
    'patches': Inputs(('riscv/patched_submodule.py',)),
    'build-driver': Inputs(('sims/program-test/Makefile.inc',)),
    'isa-source': Inputs(('sw/riscv-isa-tests', 'sw/riscv-isa-tests/*')),
    'bundle': Inputs(('sw/build/bind.py', 'sims/program-test/archive.py')),
    'evidence': Inputs(('sims/program-test/execution.py',)),
    'simulator-artifact': Inputs(('sims/program-test/artifact.py',)),
    'native-runner': Inputs(('sims/program-test/run.py', 'sims/program-test/performance.py',
                             '.github/workflows/ci-software.yml'),
                            ('bundle', 'evidence', 'simulator-artifact')),
}

BUILD_INPUTS = {
    'isa': Inputs(('sw/build/isa.mk',), ('compiler', 'elf', 'build-driver', 'isa-source')),
    'benchmark': Inputs(('sw/riscv-isa-tests-patches/*',),
                        ('compiler', 'elf', 'patches', 'build-driver', 'isa-source')),
    'coremark': Inputs(('sw/build/build-coremark.py', 'sw/coremark', 'sw/coremark/*',
                       'sw/coremark-riscv-baremetal/*'), ('compiler', 'elf', 'build-driver')),
    'embench': Inputs(('sw/build/build-embench.py', 'sw/embench-iot', 'sw/embench-iot/*',
                      'sw/embench-iot-patches/*', 'sw/embench-iot-riscv-baremetal/*'),
                     ('compiler', 'elf', 'patches', 'build-driver')),
    'bringup': Inputs(('sw/build/build-bringup-bench.py', 'sw/bringup-bench', 'sw/bringup-bench/*',
                      'sw/bringup-bench-patches/*', 'sw/bringup-bench-riscv-baremetal/*'),
                     ('compiler', 'elf', 'patches', 'build-driver')),
    'skl': Inputs(('sw/build/build-skl.py', 'sw/skl', 'sw/skl/*', 'sw/skl-patches/*',
                  'sw/skl-riscv-baremetal/*'), ('compiler', 'elf', 'patches', 'build-driver')),
    'highway': Inputs(('sw/build/build-highway.py', 'sw/highway', 'sw/highway/*',
                      'sw/highway-riscv-baremetal/*'), ('compiler', 'elf', 'build-driver')),
    'litmus': Inputs(('sw/build/build-litmus.py', 'sw/litmus-tests-riscv', 'sw/litmus-tests-riscv/*',
                     'sw/litmus-riscv-baremetal/*'),
                    ('compiler', 'elf', 'patches', 'build-driver', 'isa-source')),
    'opensbi': Inputs(('sw/build/opensbi.py', 'sw/opensbi', 'sw/opensbi/*',
                      'sw/opensbi-patches/*', 'sims/opensbi/*'),
                     ('compiler', 'elf', 'patches')),
    'linux': Inputs(('sw/build/linux.py', 'sw/buildroot', 'sw/buildroot/*',
                    'sw/buildroot-riscv/*', 'sims/linux/*'), ('build:opensbi',)),
}
INPUTS.update(('build:' + suite, inputs) for suite, inputs in BUILD_INPUTS.items())
for suite in NATIVE_SUITES:
    INPUTS['native:' + suite] = Inputs(requires=('build:' + suite, 'native-runner'))
for suite in ('litmus', 'opensbi'):
    INPUTS['platform:' + suite] = Inputs(requires=('build:' + suite, 'native-runner'))
INPUTS['image:linux'] = Inputs(requires=('build:linux', 'bundle'))
INPUTS['arch:act'] = Inputs(requires=('evidence', 'simulator-artifact'))
INPUTS['simulator:all'] = Inputs(requires=('simulator-artifact',))
for suite in ('isa', 'benchmark'):
    INPUTS['harness:' + suite] = Inputs(requires=('build:' + suite, 'native-runner'))

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
    INPUTS['adapter:' + name] = Inputs(('sw/tests/' + filename,), dependencies)


def dependencies(node, active=()):
    """Return an input closure, rejecting missing nodes or cyclic declarations."""
    if node not in INPUTS:
        raise ValueError('unknown software dependency: ' + node)
    if node in active:
        raise ValueError('cyclic software inputs: ' + ' -> '.join((*active, node)))
    result = {node}
    for dependency in INPUTS[node].requires:
        result.update(dependencies(dependency, (*active, node)))
    return result


CLOSURES = {node: dependencies(node) for node in INPUTS}
CONSUMERS = {node for node in INPUTS
             if node.partition(':')[0] in ('native', 'platform', 'harness', 'adapter', 'image', 'arch', 'simulator')}


def affected(path):
    """Find affected consumers by graph reachability, independent of declaration order."""
    changed = {node for node, inputs in INPUTS.items()
               if any(fnmatch.fnmatchcase(path, pattern) for pattern in inputs.paths)}
    return {node for node in CONSUMERS if CLOSURES[node] & changed}


def cache_patterns(suite):
    """Project only a suite's transitive build inputs into its CI cache namespace."""
    suite = 'coremark' if suite == 'coremark_scalar' else suite
    return tuple(sorted({pattern for node in CLOSURES['build:' + suite]
                         for pattern in INPUTS[node].paths}))


def cache_fingerprint(suite):
    """Hash tracked input contents and pinned gitlinks, never unrelated suite ports."""
    root = Path(__file__).resolve().parents[2]
    patterns = cache_patterns(suite)
    result = hashlib.sha256(Path(__file__).read_bytes())
    index = subprocess.check_output(['git', '-C', str(root), 'ls-files', '--stage', '-z'])
    for entry in index.decode().split('\0'):
        if not entry:
            continue
        metadata, path = entry.split('\t', 1)
        mode, revision, stage = metadata.split()
        if stage != '0':
            raise ValueError('cannot fingerprint unmerged software inputs')
        if not any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns):
            continue
        file = root / path
        content = revision.encode() if mode == '160000' else file.read_bytes()
        result.update(path.encode() + b'\0' + mode.encode() + b'\0')
        result.update(hashlib.sha256(content).digest())
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache-inputs', required=True, choices=(*BUILD_INPUTS, 'coremark_scalar'))
    args = parser.parse_args()
    print(cache_fingerprint(args.cache_inputs))


if __name__ == '__main__':
    main()
