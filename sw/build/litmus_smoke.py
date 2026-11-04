# Defines the fixed litmus smoke inventory and checks that a hosted manifest fulfills it.
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path


def read_case_names(path):
    names = [line.strip() for line in path.read_text().splitlines()
             if line.strip() and not line.lstrip().startswith('#')]
    if not names or len(names) != len(set(names)):
        raise ValueError(f'{path}: expected a nonempty list of distinct litmus cases')
    return names


def smoke_options(runtime=Path(__file__).resolve().parents[1] / 'litmus-riscv-baremetal'):
    return dict(tests=read_case_names(runtime / 'smoke-cases.txt'), runs=1, generator='litmus7')


def validate_smoke_manifest(manifest, requested):
    expected = smoke_options()
    if any(opts != expected or type(opts.get('runs')) is not int
           for opts in (requested, manifest['build_spec']['options'])):
        raise ValueError('litmus smoke build options do not match the configured smoke profile')
    runs = expected['runs']
    if type(manifest.get('runs')) is not int or manifest['runs'] != runs:
        raise ValueError(f'litmus smoke manifest must declare {runs} run(s) per case')
    entries = manifest['tests']
    names = [case['name'] for case in entries]
    if len(names) != len(set(names)):
        raise ValueError('litmus smoke manifest contains duplicate cases')
    missing, extra = set(expected['tests']) - set(names), set(names) - set(expected['tests'])
    if missing or extra:
        raise ValueError(f'litmus smoke inventory mismatch: missing={sorted(missing)}, unexpected={sorted(extra)}')
    for case in entries:
        samples = case.get('litmus_min_samples')
        if type(samples) is not int or samples != runs:
            raise ValueError(f'{case["name"]}: litmus smoke sample count must be {runs}')
