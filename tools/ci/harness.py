# Defines shared harness ELF recipes and resolves their hosted simulator bindings.
# SPDX-License-Identifier: Apache-2.0
import argparse
from dataclasses import dataclass
import json
from pathlib import Path


@dataclass(frozen=True)
class HarnessRecipe:
    test: str
    suite: str
    builder: str
    run_goal: str
    subdir: str
    options: dict
    performance: bool = False


SHARED = (
    HarnessRecipe('isa-smoke', 'isa', 'software-isa-smoke', 'isa-smoke-run', 'isa-smoke',
                  dict(isa_selection='smoke', benchmark_mode='target', benchmark_selection='single-hart',
                       benchmark_hart_count=None)),
    HarnessRecipe('tiled-mt-benchmark-test', 'benchmark', 'software-tiled-mt-benchmark', 'tiled-mt-benchmark-run',
                  'mt-benchmark/8-harts', dict(isa_selection=None, benchmark_mode='target',
                  benchmark_selection='multihart', benchmark_hart_count=8), performance=True),
)
BY_TEST = {h.test: h for h in SHARED}
BY_SUITE = {h.suite: h for h in SHARED}
if len(BY_TEST) != len(SHARED) or len(BY_SUITE) != len(SHARED):
    raise ValueError('shared harness recipes must have unique tests and suites')


def bindings(plan, soc, tests):
    """Locate exactly one resolved build for each selected shared test; ordinary tests bind nothing."""
    result = {}
    for test in tests:
        if test in BY_TEST:
            h = BY_TEST[test]
            rows = [r for r in plan['run']
                    if (r['soc'], r['suite'], r['run_target']) == (soc, h.suite, h.run_goal)]
            if len(rows) != 1:
                raise ValueError(f'{soc}/{test}: expected one shared ELF binding, found {len(rows)}')
            result[test] = dict(build_id=rows[0]['build_id'], subdir=h.subdir)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Resolve shared harness archives for a hosted simulator.')
    parser.add_argument('plan', type=Path)
    parser.add_argument('soc')
    parser.add_argument('tests', nargs='*')
    args = parser.parse_args()
    print(json.dumps(bindings(json.loads(args.plan.read_text()), args.soc, args.tests)))
