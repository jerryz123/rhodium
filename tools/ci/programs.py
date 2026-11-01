#!/usr/bin/env python3
# Groups compatible ELF builds and selected short suites without changing coverage.
# SPDX-License-Identifier: Apache-2.0
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'sw/build'))
from program_target import elf_build_spec, load_target, target_fingerprint
from .policy import PROGRAM_SHARD_COUNTS, SHORT_PROGRAM_SUITES


def program_matrices(matrix, targets, *, build_only=False):
    builds, runs = {}, []
    entries = matrix['include']
    if not entries or len({(entry['soc'], entry['suite']) for entry in entries}) != len(entries):
        raise ValueError('native matrix must contain unique, nonempty SoC/suite entries')
    for entry in entries:
        soc, suite = entry['soc'], entry['suite']
        target = targets[soc]
        if target['soc'] != soc:
            raise ValueError('native target descriptor names another SoC')
        # Options are uniform within a CI suite. Builders record their actual
        # options in the archive; consumers compare that complete specification.
        options = entry.get('options', {})
        spec = elf_build_spec(target, suite, options)
        build_id = suite + '-' + target_fingerprint(spec)
        build = builds.setdefault(build_id, dict(soc=soc, suite=suite, build_id=build_id, options=options))
        if build_only:
            build.setdefault('targets', []).append(soc)
            continue
        shard_count = PROGRAM_SHARD_COUNTS.get(suite, 1)
        runs.extend(entry | dict(build_id=build_id, shard=shard, shard_count=shard_count)
                    for shard in range(shard_count))
    return dict(build={'include': list(builds.values())}, run={'include': runs})


def harness_entries(matrix):
    """Project only shared ELF work; retain every selected config execution."""
    entries = []
    for config in matrix['include']:
        for target in config.get('software_tests', '').split():
            if target == 'isa-smoke':
                entries.append(dict(soc=config['soc'], suite='isa', run_target='isa-smoke-run',
                                    options=dict(isa_selection='smoke', benchmark_mode='target',
                                                 benchmark_selection='single-hart', benchmark_hart_count=None)))
            elif target == 'tiled-mt-benchmark-test':
                entries.append(dict(soc=config['soc'], suite='benchmark', run_target='tiled-mt-benchmark-run',
                                    options=dict(isa_selection=None, benchmark_mode='target', benchmark_selection='multihart',
                                                 benchmark_hart_count=8)))
    return {'include': entries}


def execution_groups(matrix):
    """Bundle short suites per exact simulator; keep long suites and shards separate."""
    groups = {}
    for entry in matrix['include']:
        short = entry['suite'] in SHORT_PROGRAM_SUITES and entry['shard_count'] == 1
        group = 'short' if short else entry['suite']
        key = (entry['soc'], entry['backend'], entry['variant'], group, entry['shard'])
        row = groups.setdefault(key, dict(soc=entry['soc'], backend=entry['backend'],
                                         cosim=entry['cosim'], stats=entry['stats'], variant=entry['variant'], group=group, shard=entry['shard'],
                                         shard_count=entry['shard_count'], suites=[]))
        row['suites'].append({field: entry[field] for field in ('suite', 'build_id')})
    for row in groups.values():
        if row['group'] == 'short' and len(row['suites']) == 1:
            row['group'] = row['suites'][0]['suite']
        ids = list(dict.fromkeys(suite['build_id'] for suite in row['suites']))
        row['build_pattern'] = ids[0] if len(ids) == 1 else '{' + ','.join(ids) + '}'
    return {'include': list(groups.values())}


def pipeline_matrices(builds, runs):
    """Expose independent producer families and exact-config consumer matrices."""
    build_groups, run_groups = {}, {}
    for row in builds['include']:
        suite = row['suite']
        group = 'short' if suite in SHORT_PROGRAM_SUITES and suite != 'isa' else suite
        build_groups.setdefault(group, {'include': []})['include'].append(row)
    for row in runs['include']:
        group = 'short' if row['group'] in SHORT_PROGRAM_SUITES else row['group']
        run_groups.setdefault(group, {}).setdefault(row['soc'], {'include': []})['include'].append(row)
    return dict(build_groups=build_groups, run_groups=run_groups)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--matrix', required=True, help='selected SoC/suite matrix JSON')
    parser.add_argument('--targets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--harness', action='store_true', help='group the simulator matrix shared harness ELFs')
    parser.add_argument('--group-runs', action='store_true', help='bundle short native suites per execution config')
    parser.add_argument('--build-only', action='store_true', help='group images with binding targets but no execution rows')
    args = parser.parse_args()
    if args.build_only and (args.group_runs or args.harness):
        parser.error('build-only selection cannot include execution or harness grouping')
    if args.group_runs and args.harness:
        parser.error('execution grouping is only for native software suites')
    try:
        matrix = json.loads(args.matrix)
        if args.harness:
            matrix = harness_entries(matrix)
        if args.harness and not matrix['include']:
            args.output.write_text(json.dumps(dict(build=matrix, run=matrix), indent=2) + '\n')
            return
        targets = {entry['soc']: load_target(args.targets / (entry['soc'] + '.json'))
                   for entry in matrix['include']}
        result = program_matrices(matrix, targets, build_only=args.build_only)
        if args.group_runs:
            result['run'] = execution_groups(result['run'])
            result.update(pipeline_matrices(result['build'], result['run']))
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
