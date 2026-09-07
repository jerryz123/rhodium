# Checks that ACT completion cannot pass on an empty, crashing, or contradictory simulator result.
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

RUNNER = Path(__file__).resolve().parents[1] / "arch-test" / "run.py"


class ArchTestRunnerTest(unittest.TestCase):
    def run_simulator(self, output, code):
        with tempfile.TemporaryDirectory(prefix="rhodium-act-test-") as directory:
            root = Path(directory)
            elf = root / "test with spaces.elf"
            elf.touch()
            simulator = root / "fake simulator"
            simulator.write_text(
                f"#!{sys.executable}\n"
                "import sys\n"
                "assert sys.argv[1:4] == ['+permissive', '+max-cycles=123', '+permissive-off']\n"
                "assert sys.argv[4].endswith('test with spaces.elf')\n"
                f"print({output!r})\n"
                f"sys.exit({code})\n"
            )
            simulator.chmod(0o755)
            return subprocess.run(
                [sys.executable, str(RUNNER), "--simulator", str(simulator),
                 "--max-cycles", "123", str(elf)], capture_output=True, text=True,
            )

    def test_confirmed_target_completion(self):
        result = self.run_simulator("SoC harness simulation passed", 0)
        self.assertEqual(result.returncode, 0)
        self.assertIn('RVCP-SUMMARY: TEST PASSED - Test File "test with spaces.S"', result.stdout)

    def test_exit_zero_without_completion_is_failure(self):
        result = self.run_simulator("", 0)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("RVCP-SUMMARY: TEST FAILED", result.stdout)

    def test_failure_status_overrides_pass_message(self):
        result = self.run_simulator("SoC harness simulation passed", 7)
        self.assertEqual(result.returncode, 7)
        self.assertIn("RVCP-SUMMARY: TEST FAILED", result.stdout)

    def test_simulator_failure_is_preserved(self):
        result = self.run_simulator("SoC harness reported target failure", 3)
        self.assertEqual(result.returncode, 3)
        self.assertIn("target failure", result.stdout)

    def test_timeout_is_failure(self):
        result = self.run_simulator("SoC harness simulation timed out", 1)
        self.assertEqual(result.returncode, 1)
        self.assertIn("timed out", result.stdout)


if __name__ == "__main__":
    unittest.main()
