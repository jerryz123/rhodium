#!/usr/bin/env python3
# Groups descriptor-compatible ELF recipes and their exact consumer bindings.
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'sw/build'))
from program_target import elf_build_spec, load_target, target_fingerprint
from sw.build.litmus_smoke import smoke_options
from .harness import BY_TEST
from .policy import COREMARK_SUITES


# Configurable inputs to a named Moon build: manifest option -> (environment key, default).
NATIVE_COMPILER = 'riscv64-unknown-elf-gcc'
NATIVE_OPTIONS = {
    **{s: {'iterations': ('COREMARK_ITERATIONS', 1)} for s in COREMARK_SUITES},
    'embench': {'scale': ('EMBENCH_SCALE', 1), 'local_scale': ('EMBENCH_LOCAL_SCALE', 1),
                'warmup_heat': ('EMBENCH_WARMUP_HEAT', 0)},
    'benchmark': {'benchmark_mode': ('BENCHMARK_MODE', 'target')},
}


def build_options(suite):
    # These choices distinguish compatible recipes (notably smoke/full ISA)
    # even when they use the same pinned source and compiler.
    options = {key: default for key, (_, default) in NATIVE_OPTIONS.get(suite, {}).items()}
    if suite in ('isa', 'benchmark'):
        options.update(isa_selection='full' if suite == 'isa' else None,
                       benchmark_selection='single-hart', benchmark_hart_count=None)
        options.setdefault('benchmark_mode', 'target')
    if suite == 'litmus':
        return smoke_options()
    return options


def native_build_env(suite, options):
    """Translate one resolved native build specification for both ordinary and shared ELFs."""
    configurable = NATIVE_OPTIONS.get(suite, {})
    expected = build_options(suite)
    fixed = expected.keys() - configurable.keys()
    if options.keys() != expected.keys() or any(options[k] != expected[k] for k in fixed):
        raise ValueError(f'{suite}: options do not describe a native ELF recipe: {options}')
    flags = {var: str(options[k]) for k, (var, _) in configurable.items()}
    if suite == 'bringup':
        flags['BRINGUP_BENCHMARKS'] = ''  # Named builds always run the full inventory.
    return dict(RISCV_CC=NATIVE_COMPILER, **flags)


def group_builds(entries, targets):
    builds, runs = {}, []
    if len({(entry['soc'], entry['suite']) for entry in entries}) != len(entries):
        raise ValueError('ELF requests must contain unique SoC/suite entries')
    for entry in entries:
        soc, suite = entry['soc'], entry['suite']
        target = targets[soc]
        if target['soc'] != soc:
            raise ValueError('native target descriptor names another SoC')
        # Options are uniform within a CI suite. Builders record their actual
        # options in the archive; consumers compare that complete specification.
        options = entry.get('options', build_options(suite))
        spec = elf_build_spec(target, suite, options)
        build_id = suite + '-' + target_fingerprint(spec)
        build = builds.setdefault(build_id, dict(soc=soc, suite=suite, build_id=build_id, options=options))
        build.setdefault('targets', []).append(soc)
        runs.append(entry | dict(build_id=build_id))
    return dict(build=list(builds.values()), run=runs)


def harness_entries(configs):
    """Project only shared ELF work; retain every selected config execution."""
    entries = {}
    for config in configs:
        for test in config.get('software_tests', '').split():
            if test in BY_TEST:
                h = BY_TEST[test]
                # Requests are per SoC and recipe; backends share the same ELF.
                entries[config['soc'], test] = dict(soc=config['soc'], suite=h.suite, run_target=h.run_goal,
                                                    options=h.options)
    return list(entries.values())
