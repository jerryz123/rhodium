# SPDX-License-Identifier: Apache-2.0

import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location("run_integration", Path(__file__).with_name("run-integration.py"))
integration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(integration)


class IntegrationTest(unittest.TestCase):
    def invoke(self, *options, emission_error=None):
        with tempfile.TemporaryDirectory() as work, \
             patch.object(integration.tempfile, "mkdtemp", return_value=work), \
             patch("sys.argv", ["run-integration.py", "--fixture", "sync-ram", *options]), \
             patch.object(integration.shutil, "which", return_value="/tool/verilator"), \
             patch.object(integration.Path, "is_file", return_value=True), \
             patch.object(integration, "run", return_value="artifact", side_effect=emission_error) as run, \
             patch.object(integration, "simulate") as simulate, \
             contextlib.redirect_stdout(io.StringIO()):
            integration.main()
            return run, simulate

    def test_direct_runs_only_the_direct_emitter_and_bench(self):
        run, simulate = self.invoke()
        self.assertEqual(run.call_count, 1)
        self.assertEqual(run.call_args.args[0][-2:], ["direct", "sync-ram"])
        self.assertEqual(simulate.call_count, 1)
        self.assertEqual(simulate.call_args.args[2], "direct")

    def test_manifest_comparison_keeps_direct_behavior_without_a_second_circt_simulation(self):
        run, simulate = self.invoke("--check-manifests")
        self.assertEqual([call.args[0][-2:] for call in run.call_args_list],
                         [["direct", "sync-ram"], ["circt", "sync-ram"]])
        self.assertEqual(simulate.call_count, 1)

    def test_full_differential_keeps_both_behavioral_routes(self):
        run, simulate = self.invoke("--differential")
        self.assertEqual(run.call_count, 3)
        self.assertEqual([call.args[2] for call in simulate.call_args_list], ["direct", "circt"])

    def test_emission_failures_propagate(self):
        with self.assertRaisesRegex(RuntimeError, "emission failed"):
            self.invoke("--check-manifests", emission_error=RuntimeError("emission failed"))


if __name__ == "__main__":
    unittest.main()
