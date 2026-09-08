# Exercises workload accounting, completion, deadlines, and exact-commit simulator reuse.
import hashlib
import importlib.util
import json
import struct
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1] / 'program-test'


class ProgramBuildTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('program_build', SCRIPTS / 'build.py')
        self.builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.builder)

    def test_smoke_selection_follows_capabilities_not_soc_name(self):
        target = dict(soc='mini', xlen=64, extensions=['i', 'm', 'a', 'zba', 'zbb', 'zbs', 'zicond', 'zicboz'])
        groups, names = self.builder.smoke_selection(target)
        self.assertEqual(len(names), 23)
        self.assertEqual(len(names), len(set(names)))
        self.assertIn('rv64ua-p-lrsc', names)
        self.assertIn('rv64mzicbo-p-zero', names)
        self.assertNotIn('rv64uc', groups)
        target['extensions'].append('c')
        _, compressed = self.builder.smoke_selection(target)
        self.assertEqual(set(compressed) - set(names), {'rv64uc-p-rvc'})
        target['xlen'] = 32
        with self.assertRaisesRegex(ValueError, 'RV64'):
            self.builder.smoke_selection(target)

    def test_elf_footprint_uses_memory_size_and_checks_entry(self):
        base = 0x80000000
        with tempfile.TemporaryDirectory() as directory:
            elf = Path(directory) / 'test.elf'

            def write_elf(address=base, memsz=0x8000, entry=base, filesz=1):
                header = b'\x7fELF\x02\x01\x01' + bytes(9)
                header += struct.pack('<HHIQQQIHHHHHH', 2, 243, 1, entry, 64, 0, 0, 64, 56, 1, 0, 0, 0)
                segment = struct.pack('<IIQQQQQQ', 1, 5, 120, address, address, filesz, memsz, 1)
                elf.write_bytes(header + segment + b'\x00')

            ram = [dict(base=base, size=0x8000)]
            write_elf()
            self.assertEqual(self.builder.check_elf_memory(elf, ram), [dict(address=base, memory_bytes=0x8000)])
            # A tiny file can still overflow RAM through its BSS allocation.
            for values, error in ((dict(memsz=0x8001), 'exceeds target RAM'),
                                  (dict(address=base - 1), 'exceeds target RAM'),
                                  (dict(entry=base + 0x8000), 'entry'),
                                  (dict(filesz=2), 'invalid physical')):
                write_elf(**values)
                with self.assertRaisesRegex(ValueError, error):
                    self.builder.check_elf_memory(elf, ram)
            elf.write_bytes(b'not an ELF')
            with self.assertRaisesRegex(ValueError, 'ELF64'):
                self.builder.check_elf_memory(elf, ram)

    def test_content_cache_reuses_and_repairs_binaries_and_rejects_failed_builds(self):
        spec = importlib.util.spec_from_file_location('program_build', SCRIPTS / 'build.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / 'source', root / 'output'
            (source / 'env/p').mkdir(parents=True)
            (source / 'env/p/link.ld').touch()
            count = 0
            fail = False

            def check_output(command, **kwargs):
                if command[0] == 'git':
                    return 'pinned-revision\n'
                if '--version' in command:
                    return 'test compiler 15\n'
                return 'instruction-test\n'

            def run(command, **kwargs):
                nonlocal count
                if command[0] == 'make':
                    count += 1
                    if fail:
                        return subprocess.CompletedProcess(command, 1)
                    (kwargs['cwd'] / 'instruction-test').write_bytes(b'ELF')
                return subprocess.CompletedProcess(command, 0)

            argv = ['build.py', '--suite', 'isa', '--source', str(source),
                    '--output', str(output), '--compiler', sys.executable]
            with patch.object(sys, 'argv', argv), patch.object(builder.subprocess, 'check_output', side_effect=check_output), \
                    patch.object(builder.subprocess, 'run', side_effect=run):
                builder.main()
                builder.main()
                self.assertEqual(count, 1)
                manifest = json.loads((output / 'manifest.json').read_text())
                elf = output / manifest['tests'][0]['elf']
                elf.write_bytes(b'corrupt')
                builder.main()
                self.assertEqual(count, 2)
                self.assertEqual(elf.read_bytes(), b'ELF')
                elf.unlink()
                fail = True
                with self.assertRaises(RuntimeError):
                    builder.main()
                self.assertFalse((output / 'manifest.json').exists())

    def test_smoke_cache_is_target_specific_and_checks_cached_memory_bounds(self):
        builder = self.builder
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / 'source', root / 'output'
            (source / 'env/p').mkdir(parents=True)
            (source / 'env/p/link.ld').touch()
            target = dict(soc='mini', xlen=64, extensions=['i'], ram=[dict(base=0x80000000, size=0x10000)])
            target_path = root / 'target.json'
            target_path.write_text(json.dumps(target))
            names = builder.smoke_selection(target)[1]
            header = b'\x7fELF\x02\x01\x01' + bytes(9)
            header += struct.pack('<HHIQQQIHHHHHH', 2, 243, 1, 0x80000000, 64, 0, 0, 64, 56, 1, 0, 0, 0)
            payload = header + struct.pack('<IIQQQQQQ', 1, 5, 120, 0x80000000, 0x80000000, 1, 0x9000, 1) + b'\x00'
            builds = []

            def check_output(command, **kwargs):
                if command[0] == 'git':
                    return 'pinned-revision\n'
                if '--version' in command:
                    return 'test compiler 15\n'
                self.assertIn('program_groups=rv64ui', command)
                return '\n'.join(names)

            def run(command, **kwargs):
                if command[0] == 'make':
                    builds.append(kwargs['cwd'])
                    for name in names:
                        (kwargs['cwd'] / name).write_bytes(payload)
                return subprocess.CompletedProcess(command, 0)

            argv = ['build.py', '--suite', 'isa', '--source', str(source), '--output', str(output),
                    '--compiler', sys.executable, '--target', str(target_path)]
            with patch.object(sys, 'argv', argv), patch.object(builder.subprocess, 'check_output', side_effect=check_output), \
                    patch.object(builder.subprocess, 'run', side_effect=run):
                builder.main()
                with patch.object(builder, 'check_elf_memory', wraps=builder.check_elf_memory) as checked:
                    builder.main()
                    self.assertEqual(checked.call_count, len(names))
                self.assertEqual(len(builds), 1)
                manifest = json.loads((output / 'manifest.json').read_text())
                self.assertEqual(manifest['target'], target)
                target['soc'] = 'tiled'
                target['ram'][0]['size'] = 0x8000
                target_path.write_text(json.dumps(target))
                with self.assertRaisesRegex(ValueError, 'exceeds target RAM'):
                    builder.main()
                self.assertEqual(len(builds), 2)
                self.assertNotEqual(builds[0], builds[1])
                self.assertFalse((output / 'manifest.json').exists())


class ProgramRunnerTest(unittest.TestCase):
    def run_suite(self, bodies, timeout=3, corrupt=False):
        self.directory = tempfile.TemporaryDirectory(prefix='rhodium-program-test-')
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        simulator = root / 'simulator with spaces'
        simulator.write_text(f'#!{sys.executable}\n'
                             'import sys\nfrom pathlib import Path\n'
                             "assert sys.argv[1:4] == ['+permissive', '+max-cycles=123', '+permissive-off']\n"
                             'exec(Path(sys.argv[4]).read_text())\n')
        simulator.chmod(0o755)
        tests = []
        for name, body in bodies.items():
            elf = root / name
            elf.write_text(body)
            tests.append(dict(name=name, elf=name, sha256='bad' if corrupt else hashlib.sha256(elf.read_bytes()).hexdigest()))
        manifest = root / 'manifest.json'
        manifest.write_text(json.dumps(dict(suite='test', tests=tests)))
        process = subprocess.run([sys.executable, str(SCRIPTS / 'run.py'), '--manifest', str(manifest),
                                  '--simulator', str(simulator), '--output', str(root / 'results'),
                                  '--jobs', '2', '--timeout', str(timeout), '--max-cycles', '123'],
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


class SimulatorArtifactTest(unittest.TestCase):
    def test_exact_artifact_and_mismatch_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'VTestDriver'
            binary.write_bytes(b'test binary')
            command = [sys.executable, str(SCRIPTS / 'artifact.py')]
            options = ['--binary', str(binary), '--soc', 'simple']
            subprocess.run(command + ['record'] + options, check=True)
            subprocess.run(command + ['verify'] + options, check=True)
            binary.write_bytes(b'changed binary')
            self.assertNotEqual(subprocess.run(command + ['verify'] + options, capture_output=True).returncode, 0)

    def test_prebuilt_does_not_invoke_native_build_or_racket(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'VTestDriver'
            binary.write_bytes(b'test binary')
            subprocess.run([sys.executable, str(SCRIPTS / 'artifact.py'), 'record',
                            '--binary', str(binary), '--soc', 'simple'], check=True)
            command = ['make', '-C', str(SCRIPTS.parent), 'simulator', f'PREBUILT_SIMULATOR={binary}',
                       f'PYTHON={sys.executable}', 'VERILATOR=false', 'RACKET=false', 'CIRCT_OPT=false']
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertNotEqual(subprocess.run(command + ['SOC=tiled'], capture_output=True).returncode, 0)


if __name__ == '__main__':
    unittest.main()
