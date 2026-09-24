#!/usr/bin/env python3
# Verifies complete, disjoint litmus shards before reporting full-suite outcomes.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
from pathlib import Path


STATES = ('passed', 'failed', 'timeout', 'error')


def report(manifest_path, results_dir, shard_count):
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    names = sorted(test['name'] for test in manifest['tests'])
    if not names or len(names) != len(set(names)):
        raise ValueError('manifest must have nonempty, unique test names')
    manifest_sha256 = hashlib.sha256(manifest_bytes).hexdigest()
    failures = manifest.get('build_failures', [])
    issues = []
    summaries = []
    fingerprints = set()
    totals = {state: 0 for state in STATES}
    for index in range(shard_count):
        path = results_dir / f'shard-{index}-of-{shard_count}' / 'results.json'
        if not path.is_file():
            issues.append(f'missing shard {index}: {path}')
            continue
        data = json.loads(path.read_text())
        expected = [name for offset, name in enumerate(names) if offset % shard_count == index]
        actual = [test['name'] for test in data.get('tests', [])]
        if (not data.get('complete') or data.get('pending')
                or data.get('shard_index') != index or data.get('shard_count') != shard_count
                or sorted(actual) != expected or len(actual) != len(set(actual))
                or data.get('manifest_sha256') != manifest_sha256
                or data.get('build_failures') != failures):
            issues.append(f'incomplete or mismatched shard {index}: {path}')
            continue
        fingerprint = data.get('run_fingerprint')
        if not isinstance(fingerprint, str) or len(fingerprint) != 64:
            issues.append(f'missing run fingerprint in shard {index}: {path}')
            continue
        fingerprints.add(fingerprint)
        counts = {state: sum(test['status'] == state for test in data['tests']) for state in STATES}
        if data.get('summary') != counts or sum(counts.values()) != len(expected):
            issues.append(f'invalid result counts in shard {index}: {path}')
            continue
        summaries.append(dict(index=index, results=str(path), summary=counts))
        for state in STATES:
            totals[state] += counts[state]
    if len(fingerprints) > 1:
        issues.append('shards used different simulator or run limits')
    complete = not issues and len(summaries) == shard_count
    return dict(complete=complete, expected_cases=len(names), reported_cases=sum(totals.values()),
                summary=totals, build_failures=failures, issues=issues, shards=summaries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--results-dir', type=Path, required=True)
    parser.add_argument('--shard-count', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.shard_count <= 0:
        parser.error('shard-count must be positive')
    try:
        result = report(args.manifest, args.results_dir, args.shard_count)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + '.tmp')
    temporary.write_text(json.dumps(result, indent=2) + '\n')
    temporary.replace(args.output)
    print(json.dumps(dict(complete=result['complete'], summary=result['summary'],
                          build_failures=len(result['build_failures']), issues=result['issues'])))
    return 0 if (result['complete'] and not result['build_failures']
                 and result['summary']['passed'] == result['expected_cases']) else 1


if __name__ == '__main__':
    raise SystemExit(main())
