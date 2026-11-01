#!/usr/bin/env python3
# Validates named execution counters and compares plain whole-ELF baseline maps.
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


def counter_tolerances(soc):
    """Own the counter catalog and tolerances independently of instance names."""
    policy = {'cycles': dict(percent=5, absolute=0)}
    core = selections()[soc][1]
    if core not in ('rv5stage', 'rv2wide'):
        return policy
    policy.update({f'rtl/{name}': dict(percent=5, absolute=2) for name in (
        'icache_miss', 'dcache_miss', 'mispredict_direction', 'mispredict_direct_target',
        'mispredict_indirect_target', 'mispredict_return_target', 'mispredict_nonbranch',
        'mispredict_ras_action')})
    policy.update({f'rtl/issue_{n}': dict(percent=5, absolute=0)
                   for n in range(1, 3 if core == 'rv2wide' else 2)})
    policy.update({f'rtl/retired_slot{n}': dict(percent=1, absolute=0)
                   for n in range(2 if core == 'rv2wide' else 1)})
    return policy


def counter_kind(name):
    """Select a policy by local RTL name while keeping the full key for comparison."""
    if name == 'cycles':
        return name
    if not isinstance(name, str) or not re.fullmatch(r'rtl/(?:[^/~]|~[01])+(?:/(?:[^/~]|~[01])+)*', name):
        raise ValueError('invalid counter name')
    return 'rtl/' + name.rsplit('/', 1)[-1]


def positive(value):
    return type(value) is int and value > 0


def validate_measurement(measurement, policy=None, *, complete=True):
    """Validate counts; complete simulator reports must include every required counter kind."""
    if not isinstance(measurement, dict):
        raise ValueError('measurements must map counter names to values')
    kinds = set()
    for name, count in measurement.items():
        kind = counter_kind(name)
        if type(count) is not int or not 0 <= count <= 2**64 - 1:
            raise ValueError('invalid counter value')
        if policy is not None and kind not in policy:
            raise ValueError(f'unexpected counter: {name}')
        kinds.add(kind)
    cycles = measurement.get('cycles')
    if (complete or 'cycles' in measurement) and not positive(cycles):
        raise ValueError('missing or invalid cycles')
    if cycles is not None and any(count > cycles for count in measurement.values()):
        raise ValueError('counter exceeds sampled cycles')
    if complete and policy is not None and kinds != set(policy):
        raise ValueError('missing required counters')


def load_json(text):
    """Reject duplicate names instead of silently discarding a measurement."""
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f'duplicate JSON key: {key}')
            result[key] = value
        return result
    return json.loads(text, object_pairs_hook=unique)


def baseline_expectations(baseline, inventory, policy):
    """Read plain workload-to-measurement maps; absent values have no gate yet."""
    if not isinstance(baseline, dict) or set(baseline) != set(inventory):
        raise ValueError('invalid performance baseline test names')
    for measurement in baseline.values():
        validate_measurement(measurement, policy, complete=False)
    return {name: dict(measurement=measurement, tolerances=policy) for name, measurement in baseline.items()}


def measure(text, maximum):
    lines = [line for line in text.splitlines() if line.startswith('RHODIUM-PERF:')]
    if len(lines) != 1:
        raise ValueError('missing or duplicate execution record')
    measurement = load_json(lines[0].removeprefix('RHODIUM-PERF:'))
    validate_measurement(measurement)
    if measurement['cycles'] > maximum:
        raise ValueError('whole-ELF cycle record exceeds max-cycles')
    return measurement


def enrolled(soc, suite):
    shape, _core, isa = selections().get(soc, ('', '', ''))
    return (isa == 'rva23' and suite in SUITES
            and (shape == 'simple' or shape == 'tiled' and suite == 'benchmark'))


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
    if core in ('rv5stage', 'rv2wide'):
        variant += '-stats'
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
    tolerances = counter_tolerances(identity['soc'])
    record['tolerances'] = tolerances
    if not path.exists():
        record['state'] = 'collecting'
        print(f'performance: no baseline for {identity["soc"]}/{identity["suite"]}; collecting counters', flush=True)
        return {name: dict(measurement={}, tolerances=tolerances) for name in identity['inventory']}
    try:
        expected = baseline_expectations(load_json(path.read_text()), identity['inventory'], tolerances)
    except (OSError, ValueError, KeyError, TypeError) as error:
        # Still execute the suite and upload fresh measurements for review.
        record.update(state='incompatible-baseline', reason=f'{path}: {error}')
        print(f'performance: {record["reason"]}; collecting counters before failing the gate', flush=True)
        return {}
    record['state'] = 'checked'
    # Resume identity follows the actual expectations, not a baseline fingerprint.
    record['expected'] = {name: item['measurement'] for name, item in expected.items()}
    return expected


def compare(measurement, expected):
    """Compare supplied expectations and report newly collected counters without a gate."""
    comparison, failures = {}, []
    tolerances = expected['tolerances']
    try:
        validate_measurement(measurement, tolerances)
        targets = expected['measurement']
        if not targets.keys() <= measurement.keys():
            raise ValueError('expected counters missing from simulator output')
        deltas = []
        for name, count in sorted(measurement.items()):
            value = targets.get(name)
            policy = tolerances[counter_kind(name)]
            outside = value is not None and abs(count - value) * 100 > max(policy['absolute'] * 100, value * policy['percent'])
            deltas.append(dict(name=name, count=count, expected=value, tolerance=policy, outside=outside))
            if outside:
                failures.append(f'{name}: {count}, expected {value}, tolerance ±{policy["percent"]}% / {policy["absolute"]}')
        comparison['comparisons'] = deltas
    except ValueError as error:
        failures.append(str(error))
    return comparison, failures


def write_record(output, record, payload):
    if record is None:
        return
    measured = {key: payload[key] for key in ('complete', 'pending', 'shard_index', 'shard_count', 'build_failures')}
    measured.update(performance=record, tests=[{key: result[key] for key in
                    ('name', 'status', 'functional_status', 'measurement', 'comparisons', 'reason') if key in result}
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
    return [(path, load_json(path.read_text())) for path in sorted(files)]


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
                or not result['tests'] or any(test.get('functional_status') != 'passed'
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
        workloads = {}
        tolerances = counter_tolerances(identity['soc'])
        sites = None
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
            for test in tests:
                values = test.get('measurement')
                validate_measurement(values, tolerances)
                if sites is not None and sites != values.keys():
                    raise ValueError(f'{path}: inconsistent counter inventory')
                sites = values.keys()
                workloads[test['name']] = values
        updates.append((baseline_path(directory, identity), workloads))
    # Validate the complete input set before changing any checked-in expectations.
    for path, baseline in updates:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(baseline, indent=2, sort_keys=True) + '\n')
        temporary.replace(path)
        print(f'imported {len(baseline)} ELF expectations: {path}')


def summary(records):
    for _path, result in records:
        record = result['performance']
        identity = record['identity']
        print(f'### {identity["soc"]} / {identity["suite"]} / shard {result["shard_index"]}')
        print(f'\nWhole-ELF baseline: {record["state"]}.\n')
        if 'reason' in record:
            print(record['reason'] + '\n')
        print('| ELF | Functional | Result |\n|---|---|---|')
        for test in result['tests']:
            print(f'| {test["name"]} | {test.get("functional_status", "—")} | {test["status"]} |')
        for test in result['tests']:
            items = test.get('comparisons') or [dict(name=name, count=count, expected=None, tolerance=None, outside=False)
                                               for name, count in sorted(test.get('measurement', {}).items())]
            if not items:
                continue
            print(f'\n<details><summary>{test["name"]}: counters</summary>\n')
            print('| Counter | Count | Expected | Change | Tolerance | Result |\n|---|---:|---:|---:|---|---|')
            for item in items:
                name = item['name']
                tolerance = item['tolerance']
                state = 'recorded' if tolerance is None else 'no expectation' if item['expected'] is None else 'outside' if item['outside'] else 'within'
                delta = f'{100 * (item["count"] - item["expected"]) / item["expected"]:+.2f}%' if item['expected'] else '—'
                limits = f'±{tolerance["percent"]}%, floor {tolerance["absolute"]}' if tolerance is not None else '—'
                print(f'| {name} | {item["count"]} | {item["expected"] if item["expected"] is not None else "—"} | {delta} | {limits} | {state} |')
            print('\n</details>\n')
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
