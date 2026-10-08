# Checks rsim fixture selection, build orchestration, and completion failures.
# SPDX-License-Identifier: Apache-2.0
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("rsim_runner", Path(__file__).with_name("run.py"))
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class RsimRunnerTest(unittest.TestCase):
    def invoke(self, arguments, environment=None):
        output = io.StringIO()
        with patch("sys.argv", ["run.py", *arguments]), patch.dict(os.environ, environment or {}, clear=True):
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                runner.main()
        return output.getvalue()

    def test_inventory_and_group_intersection(self):
        rows = runner.inventory()
        selected = rows[0]
        result = self.invoke(["--list", "--fixture", selected["name"], "--group", selected["group"]])
        self.assertEqual(result.splitlines(), [selected["name"]])
        self.assertEqual(self.invoke(["--list"], {"FIXTURE": selected["name"]}), result)

    def test_invalid_selection_fails_before_build(self):
        for args in (["--fixture", "does-not-exist"], ["--group", "does-not-exist"], ["--jobs", "0"]):
            with self.subTest(args=args), self.assertRaises(SystemExit):
                self.invoke(args)

    def test_rsim_build_executes_every_selected_fixture_and_checks_completion(self):
        rows = runner.inventory()[:2]
        for completion in ("PASS\n", "unfinished\n"):
            with self.subTest(completion=completion), tempfile.TemporaryDirectory() as temporary:
                work = Path(temporary)
                commands = []
                for row in rows:
                    directory = work / row['name']
                    directory.mkdir()
                    (directory / 'stale.cpp').write_text('stale')

                def command(args, log, timeout=300):
                    commands.append(list(map(str, args)))
                    if str(args[0]).endswith('run-racket.sh'):
                        for row in rows:
                            (work / row['name'] / 'model.cpp').write_text('model')
                    return completion if len(args) == 1 else ''

                with patch.object(runner, 'run', side_effect=command):
                    arguments = ['--work-dir', str(work), '--jobs', '1']
                    for row in rows:
                        arguments += ['--fixture', row['name']]
                    if completion == 'PASS\n':
                        self.invoke(arguments)
                    else:
                        with self.assertRaises(RuntimeError):
                            self.invoke(arguments)
                self.assertEqual(len(commands), 1 + 2 * len(rows))
                self.assertTrue(commands[0][0].endswith('tools/run-racket.sh'))
                for compile_command in commands[1::2]:
                    self.assertIn('-std=c++20', compile_command)
                    self.assertIn('-O2', compile_command)
                    self.assertTrue(any(arg.endswith('/model.cpp') for arg in compile_command))
                    self.assertFalse(any(arg.endswith('/stale.cpp') for arg in compile_command))


if __name__ == '__main__':
    unittest.main()
