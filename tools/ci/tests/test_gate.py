# SPDX-License-Identifier: Apache-2.0
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class ReceiptGate(unittest.TestCase):
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
