# Tests simulator workload archives, execution accounting, and exact-commit reuse.
# SPDX-License-Identifier: Apache-2.0
import hashlib
import importlib.util
import json
import os
import tarfile
from pathlib import Path
import subprocess
import sys
import tempfile
import time
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
                  contracts=None, runner_args=None):
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
        command += runner_args or []
        manifest.write_text(json.dumps(manifest_data))
        self.command = command
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
        self.assertEqual(next(test['reason'] for test in results['tests']
                              if test['name'] == 'cycle-timeout'), 'cycle-timeout')

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

    def test_litmus_histogram_is_checked_against_pinned_model(self):
        bodies = {
            'allowed': "print('Test allowed Allowed\\nHistogram (1 states)\\n    10:>1:x5=0; [x]=1;\\nSoC harness simulation passed')",
            'forbidden': "print('Test forbidden Allowed\\nHistogram (1 states)\\n    10*>1:x5=1; [x]=1;\\nSoC harness simulation passed')",
            'empty': "print('Test empty Allowed\\nHistogram (0 states)\\nSoC harness simulation passed')",
            'truncated': "print('Test truncated Allowed\\nHistogram (2 states)\\n    10:>1:x5=0; [x]=1;\\nSoC harness simulation passed')",
        }
        contract = dict(litmus_allowed_states=['1:x5=0; x=1;'], litmus_min_samples=10)
        process, results = self.run_suite(bodies, contracts={name: contract for name in bodies})
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(results['summary'], dict(passed=1, failed=3, timeout=0, error=0))
        reasons = {result['name']: result.get('reason', '') for result in results['tests']}
        self.assertIn('forbidden by pinned Herd model', reasons['forbidden'])
        self.assertIn('empty litmus histogram', reasons['empty'])
        self.assertIn('malformed litmus histogram', reasons['truncated'])
        process, results = self.run_suite(
            {'invalid': "print('SoC harness simulation passed')"},
            contracts={'invalid': dict(litmus_allowed_states=['1:x5=0;'],
                                       litmus_min_samples=0)})
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(results['summary']['error'], 1)

    def test_wall_timeout(self):
        process, results = self.run_suite({'hang': 'import time; time.sleep(30)'}, timeout=0.1)
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(results['summary']['timeout'], 1)
        self.assertEqual(results['tests'][0]['reason'], 'wall-timeout')

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

    def test_shards_are_disjoint_and_cover_every_case(self):
        bodies = {f'case-{index}': "print('SoC harness simulation passed')" for index in range(5)}
        selected = []
        for index in range(2):
            process, results = self.run_suite(bodies, runner_args=['--shard-index', str(index),
                                                                     '--shard-count', '2'])
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertTrue(results['complete'])
            self.assertEqual(results['pending'], [])
            selected += [test['name'] for test in results['tests']]
        self.assertEqual(sorted(selected), sorted(bodies))
        process, results = self.run_suite(bodies, runner_args=['--shard-index', '2',
                                                                 '--shard-count', '2'])
        self.assertNotEqual(process.returncode, 0)
        self.assertIsNone(results)

    def test_resume_reuses_only_identical_completed_run(self):
        body = "marker = Path(args[3]).with_suffix('.ran'); assert not marker.exists(); marker.touch(); print('SoC harness simulation passed')"
        process, results = self.run_suite({'once': body})
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertTrue(results['complete'])
        resumed = subprocess.run(self.command + ['--resume'], capture_output=True, text=True)
        self.assertEqual(resumed.returncode, 0, resumed.stderr)
        self.assertEqual(json.loads((Path(self.directory.name) / 'results/results.json').read_text())['summary']['passed'], 1)
        changed = subprocess.run(self.command + ['--resume', '--timeout', '7'], capture_output=True, text=True)
        self.assertNotEqual(changed.returncode, 0)
        self.assertIn('different run identity', changed.stderr)

    def test_interruption_preserves_partial_results_and_stops_simulator(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            simulator = root / 'simulator'
            simulator.write_text(f'#!{sys.executable}\nimport os, sys, time\n'
                                 "from pathlib import Path\n"
                                 "body = Path(sys.argv[-1]).read_text()\nexec(body)\n")
            simulator.chmod(0o755)
            tests = []
            for name, body in [('a-fast', "print('SoC harness simulation passed')"),
                               ('b-slow', f"Path({str(root / 'child.pid')!r}).write_text(str(os.getpid())); time.sleep(30)")]:
                elf = root / name
                elf.write_text(body)
                tests.append(dict(name=name, elf=name, sha256=hashlib.sha256(elf.read_bytes()).hexdigest()))
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps(dict(suite='test', tests=tests)))
            output = root / 'results'
            process = subprocess.Popen([sys.executable, str(SCRIPTS / 'run.py'), '--manifest', str(manifest),
                                        '--simulator', str(simulator), '--output', str(output),
                                        '--jobs', '1', '--timeout', '35'], stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True)
            try:
                for _ in range(100):
                    if (root / 'child.pid').exists():
                        break
                    time.sleep(0.05)
                self.assertTrue((root / 'child.pid').exists())
                process.terminate()
                process.communicate(timeout=5)
                self.assertEqual(process.returncode, 130)
                partial = json.loads((output / 'results.json').read_text())
                self.assertFalse(partial['complete'])
                self.assertEqual(partial['summary']['passed'], 1)
                self.assertEqual(partial['pending'], ['b-slow'])
                with self.assertRaises(ProcessLookupError):
                    os.kill(int((root / 'child.pid').read_text()), 0)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()


class ProgramShardReportTest(unittest.TestCase):
    def test_report_requires_complete_matching_shards(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps(dict(tests=[dict(name=name) for name in ('a', 'b', 'c')],
                                                build_failures=[])))
            digest = hashlib.sha256(manifest.read_bytes()).hexdigest()
            for index, names in enumerate((('a', 'c'), ('b',))):
                shard = root / f'results/shard-{index}-of-2'
                shard.mkdir(parents=True)
                shard.joinpath('results.json').write_text(json.dumps(dict(
                    complete=True, pending=[], shard_index=index, shard_count=2,
                    manifest_sha256=digest, run_fingerprint='a' * 64, build_failures=[],
                    tests=[dict(name=name, status='passed') for name in names],
                    summary=dict(passed=len(names), failed=0, timeout=0, error=0))))
            command = [sys.executable, str(SCRIPTS / 'report-shards.py'), '--manifest', str(manifest),
                       '--results-dir', str(root / 'results'), '--shard-count', '2',
                       '--output', str(root / 'summary.json')]
            process = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertEqual(json.loads((root / 'summary.json').read_text())['reported_cases'], 3)
            second = root / 'results/shard-1-of-2/results.json'
            data = json.loads(second.read_text())
            data['run_fingerprint'] = 'b' * 64
            second.write_text(json.dumps(data))
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertFalse(json.loads((root / 'summary.json').read_text())['complete'])


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
