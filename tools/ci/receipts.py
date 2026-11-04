# Collects successful CI endpoint receipts from the preceding explicit Moon invocation.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import os
from pathlib import Path

from .gate import receipt_id


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('targets', nargs='+', help='exact rhodium: targets passed to Moon')
    parser.add_argument('--sharded', action='store_true', help='collect only the primary targets assigned by Moon')
    parser.add_argument('--report', type=Path, default=Path('.moon/cache/runReport.json'))
    parser.add_argument('--output', type=Path, default=(Path(os.environ['RUNNER_TEMP']) / 'endpoint-receipts'
                                                     if 'RUNNER_TEMP' in os.environ else None))
    args = parser.parse_args()
    if args.output is None:
        parser.error('--output or RUNNER_TEMP is required')
    if len(set(args.targets)) != len(args.targets) or any(
        not target.startswith('rhodium:') or not target.removeprefix('rhodium:')
        or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-_' for c in target.removeprefix('rhodium:'))
        for target in args.targets
    ):
        parser.error('targets must be unique, explicit rhodium: task IDs')
    if not args.report.is_file():
        raise SystemExit('Moon did not produce an execution report')
    report = json.loads(args.report.read_text())
    requested = set(args.targets)
    primary = set(report['context']['primaryTargets'])
    if args.sharded:
        if (set(report['context']['initialTargets']) != requested or not primary
                or not primary <= requested):
            raise SystemExit('Moon shard report does not match the requested pool')
        if len(requested) > 1 and primary == requested:
            raise SystemExit('Moon did not partition the selected pool')
    elif primary != requested:
        raise SystemExit('Moon report does not match the requested endpoints')
    passed = {action['node']['params']['target'] for action in report['actions']
              if action['node']['action'] == 'run-task' and action['status'] in ('cached', 'passed')}
    args.output.mkdir(parents=True, exist_ok=True)
    for target in sorted(primary):
        if target in passed:
            value = receipt_id(target.removeprefix('rhodium:').replace('--', ':'))
            (args.output / (value.replace(':', '--') + '.json')).write_text(json.dumps(value) + '\n')
    missing = primary - passed
    if missing:
        raise SystemExit('Moon did not complete: ' + ', '.join(sorted(missing)))


if __name__ == '__main__':
    main()
