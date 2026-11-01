#!/usr/bin/env python3
# Validates whole-ELF execution statistics and compares cycle baselines by configuration.
# SPDX-License-Identifier: Apache-2.0
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'socs/configs'))
from selection import selections

BASELINES = Path(__file__).with_name('performance-baselines')
MEASUREMENT = 'reset-release-to-first-htif-exit'
SUITES = {'benchmark', 'coremark', 'coremark_scalar', 'embench', 'bringup', 'skl', 'highway'}
TOLERANCE_PERCENT = 5


def positive(value):
    return type(value) is int and value > 0


def measure(text, maximum):
    lines = [line for line in text.splitlines() if line.startswith('RHODIUM-PERF:')]
    if len(lines) != 1:
        raise ValueError('missing or duplicate execution record')
    record = json.loads(lines[0].removeprefix('RHODIUM-PERF:'))
    if not isinstance(record, dict) or set(record) != {'cycles', 'statistics'} or not positive(record['cycles']):
        raise ValueError('malformed execution record')
    cycles = record['cycles']
    if cycles > maximum:
        raise ValueError('whole-ELF cycle record exceeds max-cycles')
    stats = record['statistics']
    if stats is not None:
        if not isinstance(stats, list):
            raise ValueError('malformed statistics collection')
        seen = set()
        for entry in stats:
            if (not isinstance(entry, dict) or set(entry) != {'path', 'name', 'count'}
                    or not isinstance(entry['path'], list)
                    or not all(isinstance(part, str) and part for part in entry['path'])
                    or not isinstance(entry['name'], str) or not entry['name']
                    or type(entry['count']) is not int or not 0 <= entry['count'] <= min(cycles, 2**64 - 1)):
                raise ValueError('malformed statistic')
            identity = (tuple(entry['path']), entry['name'])
            if identity in seen:
                raise ValueError('duplicate statistic identity')
            seen.add(identity)
    return record


def enrolled(soc, suite):
    shape, core, isa = selections().get(soc, ('', '', ''))
    return (isa == 'rva23' and suite in SUITES
            and (shape == 'simple' and core in ('rv5stage', 'rv2wide', 'spike')
                 or shape == 'tiled' and core in ('rv5stage', 'spike') and suite == 'benchmark'))


def context(manifest, metadata):
    suite = 'bringup' if manifest['suite'] == 'bringup-bench' else manifest['suite']
    if not enrolled(metadata.get('soc'), suite):
        return None
    identity = dict(measurement=MEASUREMENT, soc=metadata['soc'], suite=suite,
                    backend=metadata.get('backend'), variant=metadata.get('variant'),
                    system=metadata.get('system'), target_fingerprint=manifest['target_fingerprint'],
                    build_spec_fingerprint=manifest.get('build_spec_fingerprint'),
                    inventory={test['name']: dict(elf_sha256=test['sha256'], harts=test.get('harts', [0]))
                               for test in manifest['tests']})
    return dict(identity=identity, simulator=metadata)


def canonical(identity):
    shape, core, _ = selections().get(identity['soc'], ('', '', ''))
    variant = 'cosim' if shape == 'simple' and core != 'spike' else 'normal'
    return (enrolled(identity['soc'], identity['suite']) and identity['backend'] == 'rsim'
            and identity['system'] == 'Linux' and identity['variant'] == variant)


def baseline_path(directory, identity):
    return directory / identity['soc'] / (identity['suite'] + '.json')


def expectations(record, directory):
    if record is None:
        return {}
    identity = record['identity']
    if not canonical(identity):
        record['state'] = 'noncanonical'
        return {}
    path = baseline_path(directory, identity)
    if not path.exists():
        record['state'] = 'unbaselined'
        print(f'performance: unbaselined {identity["soc"]}/{identity["suite"]}; collecting cycles', flush=True)
        return {}
    try:
        baseline = json.loads(path.read_text())
        expected = baseline['cycles']
        if set(expected) != set(identity['inventory']) or not all(positive(value) for value in expected.values()):
            raise ValueError('invalid performance baseline test names or cycle counts')
    except (OSError, ValueError, KeyError, TypeError) as error:
        # Still execute the suite and upload fresh measurements for review.
        record.update(state='incompatible-baseline', reason=f'{path}: {error}')
        print(f'performance: {record["reason"]}; collecting cycles before failing the gate', flush=True)
        return {}
    record['state'] = 'checked'
    # Resume identity follows the actual expectations, not a baseline fingerprint.
    record['expected_cycles'] = expected
    return expected


def compare(cycles, expected):
    delta = 100 * (cycles - expected) / expected
    outside = abs(cycles - expected) * 100 > expected * TOLERANCE_PERCENT
    return dict(expected_cycles=expected, delta_percent=delta), outside


def write_record(output, record, payload):
    if record is None:
        return
    measured = {key: payload[key] for key in ('complete', 'pending', 'shard_index', 'shard_count', 'build_failures')}
    measured.update(performance=record, tests=[{key: result[key] for key in
                    ('name', 'status', 'functional_status', 'cycles', 'statistics', 'expected_cycles', 'delta_percent', 'reason') if key in result}
                    for result in payload['tests']])
    path = output / 'performance.json'
    temporary = path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(measured, indent=2) + '\n')
    temporary.replace(path)


def load_records(paths):
    files = set()
    for path in paths:
        if path.is_dir():
            files.update(path.rglob('performance.json'))
        elif path.is_file():
            files.add(path)
        else:
            raise ValueError(f'missing results path: {path}')
    return [(path, json.loads(path.read_text())) for path in sorted(files)]


def import_results(records, directory):
    groups = {}
    commits = set()
    for path, result in records:
        record = result['performance']
        identity = record['identity']
        if not canonical(identity) or identity['measurement'] != MEASUREMENT:
            raise ValueError(f'{path}: import requires canonical Linux/rsim measurements')
        metadata = record['simulator']
        if (any(metadata.get(key) != identity[key] for key in
                ('soc', 'backend', 'variant', 'system', 'target_fingerprint'))
                or not re.fullmatch(r'[0-9a-f]{64}', metadata.get('sha256') or '')
                or not re.fullmatch(r'[0-9a-f]{40}', metadata.get('commit') or '')):
            raise ValueError(f'{path}: invalid simulator provenance')
        commits.add(metadata['commit'])
        if (not result['complete'] or result['pending'] or result['build_failures']
                or not result['tests'] or any(test.get('functional_status') != 'passed' or not positive(test.get('cycles'))
                                             for test in result['tests'])):
            raise ValueError(f'{path}: cannot import failed, incomplete, or unmeasured results')
        if (any(not re.fullmatch(r'[0-9a-f]{64}', identity.get(key) or '')
                for key in ('target_fingerprint', 'build_spec_fingerprint'))
                or any(not re.fullmatch(r'[0-9a-f]{64}', entry['elf_sha256'])
                       for entry in identity['inventory'].values())):
            raise ValueError(f'{path}: missing workload fingerprints')
        key = (identity['soc'], identity['suite'])
        groups.setdefault(key, []).append((path, result))
    if not groups:
        raise ValueError('no performance.json measurements found')
    if len(commits) != 1:
        raise ValueError('cannot import measurements from different commits together')
    updates = []
    for key, group in sorted(groups.items()):
        first = group[0][1]
        record = first['performance']
        identity = record['identity']
        count = first['shard_count']
        if not positive(count) or len(group) != count:
            raise ValueError(f'{key}: import needs exactly one complete shard set')
        indices = set()
        cycles = {}
        names = sorted(identity['inventory'])
        for path, result in group:
            index = result['shard_index']
            if (result['performance']['identity'] != identity
                    or result['performance']['simulator'] != record['simulator']
                    or result['shard_count'] != count or type(index) is not int
                    or not 0 <= index < count or index in indices):
                raise ValueError(f'{path}: mixed runs, inputs, or duplicate shards')
            indices.add(index)
            tests = result['tests']
            if sorted(test['name'] for test in tests) != names[index::count]:
                raise ValueError(f'{path}: shard test inventory differs from the complete workload')
            cycles.update({test['name']: test['cycles'] for test in tests})
        baseline = dict(cycles=cycles)
        updates.append((baseline_path(directory, identity), baseline))
    # Validate the complete input set before changing any checked-in expectations.
    for path, baseline in updates:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(baseline, indent=2, sort_keys=True) + '\n')
        temporary.replace(path)
        print(f'imported {len(baseline["cycles"])} ELF expectations: {path}')


def summary(records):
    for _path, result in records:
        record = result['performance']
        identity = record['identity']
        print(f'### {identity["soc"]} / {identity["suite"]} / shard {result["shard_index"]}')
        print(f'\nWhole-ELF cycles: {record["state"]}; tolerance ±{TOLERANCE_PERCENT}%.\n')
        if 'reason' in record:
            print(record['reason'] + '\n')
        print('| ELF | Cycles | Expected | Change | Functional | Result |\n|---|---:|---:|---:|---|---|')
        for test in result['tests']:
            delta = f'{test["delta_percent"]:+.2f}%' if 'delta_percent' in test else '—'
            print(f'| {test["name"]} | {test.get("cycles", "—")} | {test.get("expected_cycles", "—")} | {delta} | {test.get("functional_status", "—")} | {test["status"]} |')
        if not result['complete'] or result['build_failures']:
            print('\nIncomplete or build-failed workload; not importable.')
        print()


def main():
    parser = argparse.ArgumentParser(description='Import or summarize downloaded whole-ELF CI measurements.')
    parser.add_argument('command', choices=('import', 'summary'))
    parser.add_argument('--results', type=Path, nargs='+', required=True)
    parser.add_argument('--baselines', type=Path, default=BASELINES)
    args = parser.parse_args()
    try:
        records = load_records(args.results)
        if args.command == 'import':
            import_results(records, args.baselines)
        else:
            summary(records)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
