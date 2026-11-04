# Reads Moon task metadata, affected selections, and dependency graphs for CI projection.
# SPDX-License-Identifier: Apache-2.0

import json
import subprocess
import sys
import time

from .catalog import task_id
from .policy import ROOT


def target(product):
    return 'rhodium:' + task_id(product)


def query(*args, data=None):
    label = ' '.join(args[:2]) if args[0] == 'query' else args[0]
    start = time.monotonic()
    print(f'moon: starting {label}', file=sys.stderr, flush=True)
    try:
        p = subprocess.run(['moon', *args], cwd=ROOT, input=json.dumps(data) if data is not None else None,
                           text=True, stdout=subprocess.PIPE, check=True)
    finally:
        print(f'moon: {label} finished after {time.monotonic() - start:.1f}s', file=sys.stderr, flush=True)
    return json.loads(p.stdout)


def closure(roots):
    if not roots:
        return set()
    actions = query('action-graph', *map(target, sorted(roots)), '--json')['data']
    return {a['params']['target'].split(':')[1].replace('--', ':') for a in actions.values()
            if a['action'] == 'run-task'}
