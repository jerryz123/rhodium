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


def program_target(soc='simple'):
    return dict(soc=soc, xlen=64, extensions=['i', 'm'], march='rv64im',
                mabi='lp64', ram=[dict(base=0x80000000, size=0x10000)])


def target_fingerprint(target):
    return hashlib.sha256(json.dumps(target, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


class ProgramTargetTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('program_target', SCRIPTS / 'program_target.py')
        self.target = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.target)

    def test_instruction_inventory_uses_canonical_disassembly(self):
        disassembly = '''
0000000080000000 <start>:
    80000000: 4081                 c.li ra,0
    80000002: 00108093             addi ra,ra,1
    80000006: 0000                 c.unimp
'''
        with patch.object(self.target.subprocess, 'check_output', return_value=disassembly):
            inventory = self.target.instruction_inventory('objdump', Path('program.elf'))
        self.assertEqual(inventory['instruction_count'], 3)
        self.assertEqual(inventory['compressed_instruction_count'], 2)
        self.assertEqual(inventory['unknown_instruction_count'], 1)
        self.assertEqual(inventory['mnemonics'], {'addi': 1, 'c.li': 1, 'c.unimp': 1})


class ProgramBuildTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('program_build', SCRIPTS / 'build.py')
        self.builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.builder)

    def test_smoke_selection_follows_capabilities_not_soc_name(self):
        target = dict(soc='mini', xlen=64, extensions=['i', 'm', 'a', 'zba', 'zbb', 'zbs', 'zicond', 'zicboz'],
                      march='rv64ima_zba_zbb_zbs_zicond_zicboz', mabi='lp64', ram=[])
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

            def write_elf(address=base, memsz=0x8000, entry=base, filesz=1, flags=5):
                header = b'\x7fELF\x02\x01\x01' + bytes(9)
                header += struct.pack('<HHIQQQIHHHHHH', 2, 243, 1, entry, 64, 0, 0, 64, 56, 1, 0, 0, 0)
                segment = struct.pack('<IIQQQQQQ', 1, flags, 120, address, address, filesz, memsz, 1)
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
            write_elf(flags=0)
            with self.assertRaisesRegex(ValueError, 'executable'):
                self.builder.check_elf_memory(elf, ram)
            self.assertEqual(self.builder.check_elf_memory(elf, ram, require_executable_entry=False),
                             [dict(address=base, memory_bytes=0x8000)])
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
            target = dict(soc='mini', xlen=64, extensions=['i'], march='rv64i', mabi='lp64',
                          ram=[dict(base=0x80000000, size=0x10000)])
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

    def test_benchmark_target_controls_compiler_options_and_manifest(self):
        builder = self.builder
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / 'source', root / 'output'
            (source / 'env/p').mkdir(parents=True)
            (source / 'env/p/link.ld').touch()
            target = program_target()
            target['march'] = 'rv64im_zba_zicond'
            target_path = root / 'target.json'
            target_path.write_text(json.dumps(target))
            make_commands = []

            def check_output(command, **kwargs):
                if command[0] == 'git':
                    return 'pinned-revision\n'
                if '--version' in command:
                    return 'test compiler 15\n'
                raise AssertionError(command)

            def run(command, **kwargs):
                if command[0] == 'make':
                    make_commands.append(command)
                    for name in builder.BENCHMARKS:
                        (kwargs['cwd'] / (name + '.riscv')).write_bytes(b'ELF')
                return subprocess.CompletedProcess(command, 0)

            argv = ['build.py', '--suite', 'benchmark', '--source', str(source),
                    '--output', str(output), '--compiler', sys.executable,
                    '--target', str(target_path)]
            with patch.object(sys, 'argv', argv), \
                    patch.object(builder.subprocess, 'check_output', side_effect=check_output), \
                    patch.object(builder.subprocess, 'run', side_effect=run), \
                    patch.object(builder, 'probe_compiler', return_value='normalized-arch'), \
                    patch.object(builder, 'readelf_for', return_value='readelf'), \
                    patch.object(builder, 'objdump_for', return_value='objdump'), \
                    patch.object(builder, 'elf_architecture', return_value='normalized-arch'), \
                    patch.object(builder, 'instruction_inventory', return_value=dict(instruction_count=1,
                                                                                     compressed_instruction_count=0,
                                                                                     unknown_instruction_count=0,
                                                                                     mnemonics={'addi': 1})), \
                    patch.object(builder, 'check_elf_memory', return_value=[]):
                builder.main()
            self.assertEqual(len(make_commands), 1)
            self.assertIn('RISCV_MARCH=rv64im_zba_zicond', make_commands[0])
            self.assertIn('RISCV_GCC_OPTS=' + builder.FLAGS + ' -mabi=lp64', make_commands[0])
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest['target'], target)
            self.assertEqual(manifest['target_fingerprint'], target_fingerprint(target))
            self.assertEqual(manifest['benchmark_mode'], 'target')
            self.assertEqual(manifest['compiler_arch'], 'normalized-arch')
            self.assertTrue((output / 'instruction-report.json').is_file())


class ProgramRunnerTest(unittest.TestCase):
    def run_suite(self, bodies, timeout=3, corrupt=False, target=None, matching_metadata=True):
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

    def test_target_fingerprint_is_attested(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / 'VTestDriver'
            binary.write_bytes(b'test binary')
            target = root / 'target.json'
            target.write_text(json.dumps(program_target()))
            command = [sys.executable, str(SCRIPTS / 'artifact.py')]
            options = ['--binary', str(binary), '--soc', 'simple', '--target', str(target)]
            subprocess.run(command + ['record'] + options, check=True)
            subprocess.run(command + ['verify'] + options, check=True)
            changed = program_target()
            changed['march'] = 'rv64ima'
            target.write_text(json.dumps(changed))
            self.assertNotEqual(subprocess.run(command + ['verify'] + options,
                                               capture_output=True).returncode, 0)


if __name__ == '__main__':
    unittest.main()
