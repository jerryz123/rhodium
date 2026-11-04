# SPDX-License-Identifier: Apache-2.0
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.ci.actions import enrollment, select


def plan(*args):
    return json.loads(subprocess.check_output([sys.executable, '-m', 'tools.ci.actions', *args]))


class HostedCoverage(unittest.TestCase):
    def test_baseline_selects_just_its_simulator_and_four_execution_shards(self):
        run = 'program:simple-rv2wide-rva23:bringup'
        result = plan('--paths', 'sims/program-test/performance-baselines/simple-rv2wide-rva23/bringup.json')
        self.assertEqual(result['endpoints'], [*(f'{run}#{i}' for i in range(4)), 'simulator:simple-rv2wide-rva23:rsim'])

    def test_docs_skip_ci_and_unowned_changes_fail_closed(self):
        roots = enrollment()
        self.assertEqual(select(['DEVELOPING.md'], roots), set())
        self.assertEqual(select(['sw/coremark', 'unowned.xyz'], roots), roots)

    def test_mapped_smoke_runs_without_a_processor_simulator(self):
        result = plan('--paths', 'vlsi/sim/build-env.sh')
        self.assertIn('racket-cache', result['tasks'])
        self.assertEqual(result['endpoints'], ['mapped-smoke'])

    def test_gate_rejects_missing_or_unexpected_receipts(self):
        with tempfile.TemporaryDirectory() as d:
            receipts = Path(d, 'endpoint-receipts')
            receipts.mkdir()
            (receipts / 'worker.json').write_text(json.dumps('run#0'))
            for expected, error in [(['run#0'], None), (['run#0', 'run#1'], 'missing endpoint: run#1'),
                                    ([], 'unexpected endpoint: run#0')]:
                with self.subTest(expected=expected):
                    result = subprocess.run([sys.executable, '-m', 'tools.ci.gate',
                                             json.dumps(dict(endpoints=expected)),
                                             'worker success'], env=dict(os.environ, RUNNER_TEMP=d),
                                            capture_output=True, text=True)
                    if error is None:
                        self.assertEqual(result.returncode, 0, result.stderr)
                    else:
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn(error, result.stderr)
