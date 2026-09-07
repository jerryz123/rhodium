# Checks UDB-to-Sail projection, privileged-inclusive generation, and ACT completion.
import importlib.util
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
from types import ModuleType
import unittest
from unittest.mock import Mock, patch

RUNNER = Path(__file__).resolve().parents[1] / "arch-test" / "run.py"


class ArchTestConfigTest(unittest.TestCase):
    def test_configuration_includes_privileged_tests(self):
        spec = importlib.util.spec_from_file_location("act_configure", RUNNER.with_name("configure.py"))
        configure = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(configure)
        config = configure.test_config("simple-soc", "gcc", "objdump", "/tmp/sail", "/tmp/udb.yaml")
        self.assertIs(config["include_priv_tests"], True)
        self.assertEqual(config["udb_config"], str(Path("/tmp/udb.yaml").resolve()))

    def test_architecture_settings_come_from_core_profile(self):
        spec = importlib.util.spec_from_file_location("act_configure", RUNNER.with_name("configure.py"))
        configure = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(configure)
        for width, version, svade in ((0, "= 1.12.0", True), (7, "1.12", False), (16, "1.11", False)):
            with self.subTest(asid_width=width, privileged_version=version, svade=svade):
                params = dict(MXLEN=64, NUM_PMP_ENTRIES=0, MISALIGNED_LDST=False,
                              MISALIGNED_LDST_EXCEPTION_PRIORITY="high", M_MODE_ENDIANNESS="little",
                              HPM_COUNTER_EN=[False] * 32, MCOUNTENABLE_EN=[False] * 32,
                              SCOUNTENABLE_EN=[False] * 32, MTVEC_MODES=[0, 1], STVEC_MODES=[0, 1],
                              MTVEC_BASE_ALIGNMENT_DIRECT=4, MSTATUS_FS_LEGAL_VALUES=[0],
                              MSTATUS_VS_LEGAL_VALUES=[0], PHYS_ADDR_WIDTH=44, ASID_WIDTH=width,
                              LRSC_FAIL_ON_NON_EXACT_LRSC=True)
                for parameter in ("REPORT_ENCODING_IN_MTVAL_ON_ILLEGAL_INSTRUCTION",
                                  "REPORT_VA_IN_MTVAL_ON_BREAKPOINT", "REPORT_VA_IN_MTVAL_ON_LOAD_MISALIGNED",
                                  "REPORT_VA_IN_MTVAL_ON_STORE_AMO_MISALIGNED",
                                  "REPORT_VA_IN_MTVAL_ON_INSTRUCTION_MISALIGNED"):
                    params[parameter] = True
                default = {
                    "extensions": {"Svade": {"supported": not svade}, "V": {},
                                   "Stateen": {"Smstateen": {}, "Ssstateen": {}}},
                    "base": {"mtvec": {"direct": {}, "vectored": {}},
                             "stvec": {"direct": {}, "vectored": {}}, "mstatus": {}, "xtval_nonzero": {},
                             "medeleg": {"delegatable_bits": {"len": 64, "value": "0xfc_b7ff"}}},
                    "memory": {"asidlen": 16, "pmp": {}, "misaligned": {"exceptions": {}}, "regions": [
                        {"attributes": {"mem_type": "MainMemory", "cacheable": True}},
                        {"attributes": {"mem_type": "IO", "cacheable": False}},
                    ]},
                    "platform": {"reservation": {}},
                }
                extensions = [{"name": "Sm", "version": version}]
                if svade:
                    extensions.append({"name": "Svade", "version": "= 1.0.0"})
                udb = {"params": params, "implemented_extensions": extensions}
                config = configure.sail_config(default, udb, 0x80000000, 0x40000000)
                self.assertEqual(config["memory"]["asidlen"], width)
                self.assertEqual(config["base"]["privileged_isa_version"],
                                 "Privileged_ISA_1_11" if version == "1.11" else "Privileged_ISA_1_12")
                self.assertIs(config["extensions"]["Svade"]["supported"], svade)
                self.assertEqual(config["memory"]["misaligned"]["exceptions"]["lrsc"],
                                 {"Some": "AlignmentException"})
                self.assertEqual(int(config["base"]["medeleg"]["delegatable_bits"]["value"], 0), 0xcb3ff)


class ArchTestGenerationTest(unittest.TestCase):
    def test_shards_cover_inventory_exactly_once_and_replace_stale_links(self):
        spec = importlib.util.spec_from_file_location('act_shard', RUNNER.with_name('shard.py'))
        sharder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(sharder)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            elfs = root / 'elfs'
            (elfs / 'nested').mkdir(parents=True)
            for index in range(11):
                (elfs / 'nested' / f'{index}.elf').touch()
            partitions = [sharder.partition(elfs, root / f'shard-{i}/elfs', i, 4) for i in range(4)]
            combined = [elf for group in partitions for elf in group]
            self.assertEqual(len(combined), len(set(combined)))
            self.assertEqual(set(combined), set(elfs.resolve().rglob('*.elf')))
            (elfs / 'nested/0.elf').unlink()
            sharder.partition(elfs, root / 'shard-0/elfs', 0, 4)
            self.assertFalse((root / 'shard-0/elfs/nested/0.elf').is_symlink())
            self.assertEqual(len(list(elfs.rglob('*.elf'))), 10)
            foreign = root / 'foreign/elfs'
            foreign.mkdir(parents=True)
            (foreign / 'keep.elf').symlink_to(elfs / 'nested/1.elf')
            with self.assertRaises(ValueError):
                sharder.partition(elfs, foreign, 0, 4)
            self.assertTrue((foreign / 'keep.elf').is_symlink())

    def test_entry_point_requires_sail_014_without_replacing_version_check(self):
        config = ModuleType("act.config")
        config.REQUIRED_SAIL_VERSION = "0.13.1"
        config.check_ref_model_version = Mock()
        version_check = config.check_ref_model_version
        act = ModuleType("act")
        act.config = config
        cli = ModuleType("act.act")
        cli.main = Mock()
        with patch.dict(sys.modules, {"act": act, "act.config": config, "act.act": cli}):
            runpy.run_path(str(RUNNER.with_name("build.py")), run_name="__main__")
        self.assertEqual(config.REQUIRED_SAIL_VERSION, "0.14")
        self.assertIs(config.check_ref_model_version, version_check)
        cli.main.assert_called_once_with()

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
            act = root / "venv/bin/python"
            act.parent.mkdir(parents=True)
            act.write_text(
                f"#!{sys.executable}\n"
                "# Emulates ACT generation to check the Make-to-ACT contract.\n"
                "import sys\nfrom pathlib import Path\n"
                "assert Path(sys.argv[1]).name == 'build.py'\n"
                "assert sys.argv[sys.argv.index('--extensions') + 1] == 'all'\n"
                "assert '--keep-going' in sys.argv\n"
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
    def test_make_runs_only_its_shard_and_preserves_upstream_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            elfs = root / 'work/simple-soc/simple-soc/elfs'
            elfs.mkdir(parents=True)
            for index in range(8):
                (elfs / f'{index}.elf').touch()
            binary = root / 'VTestDriver'
            binary.write_bytes(b'fake native artifact')
            artifact = RUNNER.parents[1] / 'program-test/artifact.py'
            subprocess.run([sys.executable, str(artifact), 'record', '--binary', str(binary), '--soc', 'simple'], check=True)
            (root / 'run_tests.py').write_text(
                '# Emulates upstream ACT execution for the Make/shard/result contract.\n'
                'import os, sys\nfrom pathlib import Path\n'
                'elfs = Path(sys.argv[-1])\n'
                "names = sorted(path.name for path in elfs.rglob('*.elf'))\n"
                "assert names == ['1.elf', '5.elf'], names\n"
                "summary = ''.join(f'{Path(n).stem}.log  RVCP-SUMMARY: TEST PASSED - Test File \\\"test.S\\\"\\n' for n in names)\n"
                "(elfs.parent / 'summary.log').write_text(summary)\n"
                "sys.exit(int(os.environ.get('FAKE_ACT_EXIT', '0')))\n"
            )
            command = ['make', '-C', str(RUNNER.parents[1]), 'arch-test-run',
                       f'ACT_DIR={root}', f'ACT_BUILD_ROOT={root}', f'ACT_PYTHON={sys.executable}',
                       f'PYTHON={sys.executable}', f'PREBUILT_SIMULATOR={binary}', 'ACT_SHARDS=4', 'ACT_SHARD=1']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue((elfs.parent / 'shards/1/results.json').is_file())
            result = subprocess.run(command, env={**os.environ, 'FAKE_ACT_EXIT': '7'}, capture_output=True)
            self.assertNotEqual(result.returncode, 0)

    def test_report_rejects_missing_results(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            elfs = root / 'elfs'
            elfs.mkdir()
            (elfs / 'complete.elf').touch()
            (elfs / 'missing.elf').touch()
            (root / 'summary.log').write_text('complete.log  RVCP-SUMMARY: TEST PASSED - Test File "complete.S"\n')
            result = subprocess.run([sys.executable, str(RUNNER.with_name('report.py')), str(elfs)], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b'"error": 1', result.stdout)
            self.assertTrue((root / 'junit.xml').is_file())

    def test_report_distinguishes_cycle_timeout_from_target_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'elfs').mkdir()
            (root / 'logs').mkdir()
            (root / 'elfs/limited.elf').touch()
            (root / 'logs/limited.log').write_text('SoC harness simulation timed out\n')
            (root / 'summary.log').write_text('limited.log  RVCP-SUMMARY: TEST FAILED - Test File "limited.S"\n')
            result = subprocess.run([sys.executable, str(RUNNER.with_name('report.py')), str(root / 'elfs')], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b'"timeout": 1', result.stdout)

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
