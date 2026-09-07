# Checks ACT completion and full-suite generation without stale generated ELFs.
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

RUNNER = Path(__file__).resolve().parents[1] / "arch-test" / "run.py"


class ArchTestGenerationTest(unittest.TestCase):
    def test_generates_all_supported_tests_and_replaces_only_elf_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            elf_dir = root / "work/simple-soc/simple-soc/elfs"
            elf_dir.mkdir(parents=True)
            (elf_dir / "old.elf").touch()
            (elf_dir / "old.elf.objdump").touch()
            other_elf = root / "work/other/keep.elf"
            other_elf.parent.mkdir(parents=True)
            other_elf.touch()
            act = root / "venv/bin/act"
            act.parent.mkdir(parents=True)
            act.write_text(
                f"#!{sys.executable}\n"
                "# Emulates ACT generation to check the Make-to-ACT contract.\n"
                "import sys\nfrom pathlib import Path\n"
                "assert sys.argv[sys.argv.index('--extensions') + 1] == 'all'\n"
                f"elf_dir = Path({str(elf_dir)!r})\n"
                "assert not list(elf_dir.rglob('*.elf'))\n"
                "(elf_dir / 'selected.elf').touch()\n"
            )
            act.chmod(0o755)
            result = subprocess.run(
                ["make", "-o", "arch-test-config", "arch-test-elfs",
                 f"ACT_DIR={root}", f"ACT_VENV={root / 'venv'}", f"ACT_BUILD_ROOT={root}"],
                cwd=RUNNER.parents[1], capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual([p.name for p in elf_dir.glob('*.elf')], ["selected.elf"])
            self.assertTrue((elf_dir / "old.elf.objdump").exists())
            self.assertTrue(other_elf.exists())


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
