# Tests simulator workload archives, execution accounting, and exact-commit reuse.
# SPDX-License-Identifier: Apache-2.0
import hashlib
import importlib.util
import json
import tarfile
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1] / 'program-test'


def program_target(soc='single-core-rv5stage-soc'):
    return dict(soc=soc, xlen=64, harts=[0], extensions=['i', 'm'], march='rv64im',
                mabi='lp64', clock_frequency_hz=100000000,
                ram=[dict(base=0x80000000, size=0x10000)])


def target_fingerprint(target):
    return hashlib.sha256(json.dumps(target, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


class ProgramArchiveTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('program_archive', SCRIPTS / 'archive.py')
        self.archive = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.archive)

    def test_packages_manifest_binaries_with_replay_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest_root = root / 'suite'
            binary_root = manifest_root / 'build' / 'key'
            binary_root.mkdir(parents=True)
            tests = []
            for name in ('rv64ui-p-add', 'picojpeg.riscv'):
                binary = binary_root / name
                binary.write_bytes(b'\x7fELF' + name.encode())
                tests.append(dict(name=name, elf=f'build/key/{name}',
                                  sha256=hashlib.sha256(binary.read_bytes()).hexdigest()))
            manifest_path = manifest_root / 'manifest.json'
            manifest_path.write_text(json.dumps(dict(tests=tests)))
            archive_path = root / 'artifacts' / 'suite.tar.gz'
            self.archive.package(manifest_path, archive_path)
            with tarfile.open(archive_path) as archive:
                self.assertEqual(archive.getnames(),
                                 ['manifest.json', 'build/key/picojpeg.riscv', 'build/key/rv64ui-p-add'])
                for test in tests:
                    payload = archive.extractfile(test['elf']).read()
                    self.assertEqual(hashlib.sha256(payload).hexdigest(), test['sha256'])

    def test_rejects_missing_corrupt_or_outside_binaries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / 'program.riscv'
            binary.write_bytes(b'\x7fELF')
            manifest_path = root / 'manifest.json'
            archive_path = root / 'program.tar.gz'
            for name, digest, message in (
                    ('missing.riscv', hashlib.sha256(b'\x7fELF').hexdigest(), 'missing'),
                    ('program.riscv', '0' * 64, 'checksum mismatch'),
                    ('../program.riscv', hashlib.sha256(b'\x7fELF').hexdigest(), 'invalid')):
                manifest_path.write_text(json.dumps(dict(tests=[dict(elf=name, sha256=digest)])))
                with self.assertRaisesRegex(ValueError, message):
                    self.archive.package(manifest_path, archive_path)
                self.assertFalse(archive_path.exists())
class ProgramRunnerTest(unittest.TestCase):
    def run_suite(self, bodies, timeout=3, corrupt=False, target=None, matching_metadata=True,
                  contracts=None):
        self.directory = tempfile.TemporaryDirectory(prefix='rhodium-program-test-')
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        simulator = root / 'simulator with spaces'
        simulator.write_text(f'#!{sys.executable}\n'
                             'import sys\nfrom pathlib import Path\n'
                             'args = sys.argv[1:]\n'
                             "if args[0].startswith('+boot-harts='): args = args[1:]\n"
                             "assert args[:3] == ['+permissive', '+max-cycles=123', '+permissive-off']\n"
                             'exec(Path(args[3]).read_text())\n')
        simulator.chmod(0o755)
        tests = []
        for name, body in bodies.items():
            elf = root / name
            elf.write_text(body)
            test = dict(name=name, elf=name,
                        sha256='bad' if corrupt else hashlib.sha256(elf.read_bytes()).hexdigest())
            if contracts and name in contracts:
                test.update(contracts[name])
            tests.append(test)
        manifest = root / 'manifest.json'
        manifest_data = dict(suite='test', tests=tests)
        command = [sys.executable, str(SCRIPTS / 'run.py'), '--manifest', str(manifest),
                   '--simulator', str(simulator), '--output', str(root / 'results'),
                   '--jobs', '2', '--timeout', str(timeout), '--max-cycles', '123']
        if target:
            fingerprint = target_fingerprint(target)
            manifest_data.update(target=target, target_fingerprint=fingerprint)
            metadata = simulator.with_suffix('.json')
            metadata.write_text(json.dumps(dict(soc=target['soc'],
                                                target_fingerprint=fingerprint if matching_metadata else 'wrong',
                                                sha256=hashlib.sha256(simulator.read_bytes()).hexdigest())))
            command += ['--simulator-metadata', str(metadata)]
        manifest.write_text(json.dumps(manifest_data))
        process = subprocess.run(command,
                                 capture_output=True, text=True)
        summary = root / 'results/results.json'
        return process, json.loads(summary.read_text()) if summary.exists() else None

    def test_runs_all_tests_despite_failure_and_requires_completion(self):
        process, results = self.run_suite({
            'success': "print('SoC harness simulation passed')",
            'false success': 'pass',
            'failed': "print('SoC harness simulation passed'); sys.exit(4)",
            'cycle-timeout': "print('SoC harness simulation timed out'); sys.exit(1)",
        })
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(results['summary'], dict(passed=1, failed=2, timeout=1, error=0))
        self.assertEqual(len(results['tests']), 4)

    def test_success_and_junit(self):
        process, results = self.run_suite({'pass': "print('SoC harness simulation passed')"})
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertEqual(results['summary']['passed'], 1)
        self.assertTrue((Path(self.directory.name) / 'results/junit.xml').is_file())

    def test_success_marker_may_follow_uart_output_on_the_same_line(self):
        process, results = self.run_suite({'pass': "print('uartSoC harness simulation passed')"})
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertEqual(results['summary']['passed'], 1)

    def test_manifest_output_contract_is_enforced(self):
        bodies = {
            'matched': "print('expected marker'); print('SoC harness simulation passed')",
            'missing': "print('SoC harness simulation passed')",
            'forbidden': "print('bad marker'); print('SoC harness simulation passed')",
        }
        contracts = {
            'matched': dict(required_output=['expected marker'], forbidden_output=['bad marker']),
            'missing': dict(required_output=['expected marker']),
            'forbidden': dict(forbidden_output=['bad marker']),
        }
        process, results = self.run_suite(bodies, contracts=contracts)
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(results['summary'], dict(passed=1, failed=2, timeout=0, error=0))
        reasons = {result['name']: result.get('reason', '') for result in results['tests']}
        self.assertIn('missing output', reasons['missing'])
        self.assertIn('forbidden output', reasons['forbidden'])

    def test_wall_timeout(self):
        process, results = self.run_suite({'hang': 'import time; time.sleep(30)'}, timeout=0.1)
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(results['summary']['timeout'], 1)

    def test_changed_elf_is_error(self):
        process, results = self.run_suite({'changed': 'pass'}, corrupt=True)
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(results['summary']['error'], 1)

    def test_empty_suite_is_error(self):
        process, results = self.run_suite({})
        self.assertNotEqual(process.returncode, 0)
        self.assertIsNone(results)

    def test_target_bound_suite_requires_matching_simulator(self):
        target = program_target()
        process, results = self.run_suite({'pass': "print('SoC harness simulation passed')"}, target=target)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertEqual(results['summary']['passed'], 1)
        process, results = self.run_suite({'pass': "print('SoC harness simulation passed')"},
                                          target=target, matching_metadata=False)
        self.assertNotEqual(process.returncode, 0)
        self.assertIsNone(results)

    def test_per_test_harts_are_validated_and_passed_to_simulator(self):
        target = program_target()
        target['harts'] = [0, 1, 2]
        process, results = self.run_suite(
            {'parallel': "print('SoC harness simulation passed')"}, target=target,
            contracts={'parallel': dict(harts=[0, 2])})
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertEqual(results['tests'][0]['command'][1], '+boot-harts=0,2')
        for harts in ([2, 0], [0, 3], []):
            process, results = self.run_suite(
                {'invalid': "print('SoC harness simulation passed')"}, target=target,
                contracts={'invalid': dict(harts=harts)})
            self.assertNotEqual(process.returncode, 0)
            self.assertIsNone(results)


class SimulatorArtifactTest(unittest.TestCase):
    def test_exact_artifact_and_mismatch_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'VTestDriver'
            binary.write_bytes(b'test binary')
            command = [sys.executable, str(SCRIPTS / 'artifact.py')]
            options = ['--binary', str(binary), '--soc', 'single-core-rv5stage-soc']
            subprocess.run(command + ['record'] + options, check=True)
            subprocess.run(command + ['verify'] + options, check=True)
            binary.write_bytes(b'changed binary')
            self.assertNotEqual(subprocess.run(command + ['verify'] + options, capture_output=True).returncode, 0)

    def test_prebuilt_does_not_invoke_native_build_or_racket(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'VTestDriver'
            binary.write_bytes(b'test binary')
            subprocess.run([sys.executable, str(SCRIPTS / 'artifact.py'), 'record',
                            '--binary', str(binary), '--soc', 'single-core-rv5stage-soc'], check=True)
            command = ['make', '-C', str(SCRIPTS.parent), 'simulator', 'SOC=single-core-rv5stage-soc',
                       f'PREBUILT_SIMULATOR={binary}',
                       f'PYTHON={sys.executable}', 'VERILATOR=false', 'RACKET=false', 'CIRCT_OPT=false']
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertNotEqual(subprocess.run(command + ['SOC=tiled-rv5stage-soc'], capture_output=True).returncode, 0)

    def test_target_fingerprint_is_attested(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / 'VTestDriver'
            binary.write_bytes(b'test binary')
            target = root / 'target.json'
            target.write_text(json.dumps(program_target()))
            command = [sys.executable, str(SCRIPTS / 'artifact.py')]
            options = ['--binary', str(binary), '--soc', 'single-core-rv5stage-soc', '--target', str(target)]
            subprocess.run(command + ['record'] + options, check=True)
            subprocess.run(command + ['verify'] + options, check=True)
            changed = program_target()
            changed['march'] = 'rv64ima'
            target.write_text(json.dumps(changed))
            self.assertNotEqual(subprocess.run(command + ['verify'] + options,
                                               capture_output=True).returncode, 0)


if __name__ == '__main__':
    unittest.main()
