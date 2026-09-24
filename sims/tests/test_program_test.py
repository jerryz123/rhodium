# Exercises workload accounting, completion, deadlines, and exact-commit simulator reuse.
# SPDX-License-Identifier: Apache-2.0
import hashlib
import importlib.util
import json
import struct
import tarfile
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1] / 'program-test'


def program_target(soc='single-core-rv5stage-soc'):
    return dict(soc=soc, xlen=64, harts=[0], extensions=['i', 'm'], march='rv64im',
                mabi='lp64', clock_frequency_hz=100000000,
                ram=[dict(base=0x80000000, size=0x10000)])


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

    def test_target_requires_positive_clock_frequency(self):
        target = program_target()
        self.assertEqual(self.target.validate_target(target), target)
        for value in (0, -1, '100000000'):
            target['clock_frequency_hz'] = value
            with self.assertRaisesRegex(ValueError, 'invalid program target'):
                self.target.validate_target(target)

    def test_virtual_memory_capabilities_are_validated_together(self):
        target = program_target()
        target.update(mmu_mode='sv39', privilege_modes=['m', 's', 'u'])
        self.assertEqual(self.target.validate_target(target), target)
        for changes in ({'privilege_modes': ['m', 's', 's']},
                        {'privilege_modes': ['s', 'u']},
                        {'mmu_mode': 'sv48'},
                        {'mmu_mode': 'sv39', 'xlen': 32}):
            invalid = target | changes
            with self.assertRaises(ValueError):
                self.target.validate_target(invalid)
        target.pop('privilege_modes')
        with self.assertRaisesRegex(ValueError, 'declared together'):
            self.target.validate_target(target)

    def test_optional_boot_and_hart_metadata_is_validated(self):
        target = program_target()
        target.update(harts=[0], boot={'payload_address': 0x80000000})
        self.assertEqual(self.target.validate_target(target), target)
        target['harts'] = [0, 0]
        with self.assertRaisesRegex(ValueError, 'invalid program target descriptor'):
            self.target.validate_target(target)
        target['harts'] = [0]
        target['boot'] = {'payload_address': -1}
        with self.assertRaisesRegex(ValueError, 'boot description'):
            self.target.validate_target(target)

    def test_target_requires_canonical_hart_ids(self):
        for harts in ([], [1, 0], [0, 0], [-1], [False]):
            target = program_target()
            target['harts'] = harts
            with self.assertRaisesRegex(ValueError, 'invalid program target descriptor'):
                self.target.validate_target(target)


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
class CoreMarkBuildTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('coremark_build', SCRIPTS / 'build-coremark.py')
        self.builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.builder)

    def test_short_workload_checks_performance_seed_crcs(self):
        self.assertEqual(self.builder.NAME, 'coremark.riscv')
        self.assertIn('2K performance run parameters for coremark.', self.builder.REQUIRED_OUTPUT)
        self.assertIn('[0]crclist       : 0xe714', self.builder.REQUIRED_OUTPUT)
        self.assertIn('ERROR! list crc', self.builder.FORBIDDEN_OUTPUT)
        self.assertIn('ERROR: ee_u32 is not a 32b datatype!', self.builder.FORBIDDEN_OUTPUT)

    def test_scalar_variant_disables_auto_vectorization_without_relaxing_checks(self):
        self.assertEqual(self.builder.SCALAR_NAME, 'coremark_scalar.riscv')
        self.assertEqual(self.builder.SCALAR_FLAGS, ('-fno-tree-vectorize',))
        self.assertIn('[0]crcmatrix     : 0x1fd7', self.builder.REQUIRED_OUTPUT)
        self.assertIn('ERROR: ee_u32 is not a 32b datatype!', self.builder.FORBIDDEN_OUTPUT)

    def test_linker_template_uses_target_ram(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            template = root / 'link.ld.in'
            output = root / 'link.ld'
            template.write_text('origin=@RAM_ORIGIN@ length=@RAM_LENGTH@\n')
            self.builder.write_linker(template, output, dict(base=0x80000000, size=0x20000))
            self.assertEqual(output.read_text(), 'origin=0x80000000 length=0x20000\n')


class EmbenchBuildTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('embench_build', SCRIPTS / 'build-embench.py')
        self.builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.builder)

    def populate_source(self, root):
        source = root / 'source'
        (source / 'support').mkdir(parents=True)
        for name in ('sconstruct.py', 'support/main.c', 'support/beebsc.c',
                     'support/support.h', 'support/beebsc.h'):
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('upstream\n')
        for benchmark in self.builder.BENCHMARKS:
            path = source / 'src' / benchmark / (benchmark + '.c')
            path.parent.mkdir(parents=True)
            path.write_text('benchmark\n' if benchmark == 'xgboost'
                            else '#define LOCAL_SCALE_FACTOR 2\nbenchmark\n')
        (source / 'src/xgboost/testbench.c').write_text('''
#define LOCAL_SCALE_FACTOR 2
// Run inference with all samples specified in xgboost.c
        size_t correct = 0;
for (volatile size_t i = 0; i < SAMPLES_IN_FILE; i++)
uint8_t predicted = predict(X_test[i]);
uint8_t label = Y_test[i];
// r is the number of errors therefore if r = 0 then output a 1 for correct
return r >= SAMPLES_IN_FILE * (LOCAL_SCALE_FACTOR, GLOBAL_SCALE_FACTOR / 12);
''')
        return source

    def test_inventory_is_complete_and_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            source = self.populate_source(Path(directory))
            with patch.object(self.builder.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)):
                sources = self.builder.verify_upstream(source)
            self.assertEqual(tuple(sources), self.builder.BENCHMARKS)
            (source / 'src' / self.builder.BENCHMARKS[0] / (self.builder.BENCHMARKS[0] + '.c')).unlink()
            with self.assertRaisesRegex(ValueError, 'has no C sources'):
                self.builder.benchmark_sources(source)
            (source / 'src' / 'unexpected').mkdir()
            with self.assertRaisesRegex(ValueError, 'inventory changed'):
                self.builder.benchmark_sources(source)

    def test_xgboost_profile_is_bounded_and_checks_exact_result(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sources = self.builder.benchmark_sources(self.populate_source(root))
            generated, scales = self.builder.materialize_sources(sources, root / 'generated', 1)
            testbench = next(path for path in generated['xgboost'] if path.name == 'testbench.c')
            text = testbench.read_text()
            self.assertIn('functional_samples[] = {3, 2, 1, 18, 4, 8, 11, 0, 61, 7}', text)
            self.assertIn('X_test[functional_samples[i]]', text)
            self.assertIn('Y_test[functional_samples[i]]', text)
            self.assertIn('r == 9 * LOCAL_SCALE_FACTOR * GLOBAL_SCALE_FACTOR', text)
            self.assertNotIn('(LOCAL_SCALE_FACTOR, GLOBAL_SCALE_FACTOR / 12)', text)
            self.assertEqual(scales['xgboost'], 2)

            source_testbench = root / 'source/src/xgboost/testbench.c'
            source_testbench.write_text(source_testbench.read_text().replace('SAMPLES_IN_FILE; i++',
                                                                              'samples; i++'))
            sources = self.builder.benchmark_sources(root / 'source')
            with self.assertRaisesRegex(ValueError, 'functional-profile source marker'):
                self.builder.materialize_sources(sources, root / 'changed', 1)

    def test_builds_every_workload_and_reuses_only_verified_cache(self):
        builder = self.builder
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = self.populate_source(root)
            port = root / 'port'
            port.mkdir()
            for name in ('boardsupport.c', 'boardsupport.h', 'htif.c', 'start.S', 'link.ld.in'):
                text = 'origin=@RAM_ORIGIN@ length=@RAM_LENGTH@\n' if name == 'link.ld.in' else 'port\n'
                (port / name).write_text(text)
            output = root / 'output'
            target_path = root / 'target.json'
            target_path.write_text(json.dumps(program_target()))
            builds = []

            def check_output(command, **kwargs):
                if command[0] == 'git':
                    return 'upstream-head\n'
                if '--version' in command:
                    return 'test compiler 15\n'
                raise AssertionError(command)

            def run(command, **kwargs):
                if command[0] != 'git':
                    builds.append(command)
                    Path(command[command.index('-o') + 1]).write_bytes(b'ELF')
                return subprocess.CompletedProcess(command, 0)

            argv = ['build-embench.py', '--source', str(source), '--port', str(port),
                    '--output', str(output), '--compiler', sys.executable,
                    '--target', str(target_path), '--scale', '1', '--local-scale', '1',
                    '--warmup-heat', '0']
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
                builder.main()
                self.assertEqual(len(builds), len(builder.BENCHMARKS))
                manifest = json.loads((output / 'manifest.json').read_text())
                self.assertEqual([test['name'] for test in manifest['tests']], list(builder.BENCHMARKS))
                self.assertEqual(manifest['mode'], 'functional')
                self.assertFalse(manifest['scoring'])
                self.assertEqual(manifest['local_scale'], 1)
                self.assertEqual(set(manifest['upstream_local_scales'].values()), {2})
                self.assertEqual(manifest['functional_profiles'],
                                 {'xgboost': {'sample_indices': [3, 2, 1, 18, 4, 8, 11, 0, 61, 7],
                                              'expected_correct': 9}})
                self.assertEqual(manifest['warmup_heat'], 0)
                first = output / manifest['tests'][0]['elf']
                first.write_bytes(b'corrupt')
                builder.main()
                self.assertEqual(len(builds), 2 * len(builder.BENCHMARKS))


class ProgramBuildTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('program_build', SCRIPTS / 'build.py')
        self.builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.builder)

    def test_smoke_selection_follows_capabilities_not_soc_name(self):
        target = dict(soc='mini-rv5stage-soc', xlen=64, extensions=['i', 'm', 'a', 'zba', 'zbb', 'zbs', 'zicond', 'zicboz'],
                      march='rv64ima_zba_zbb_zbs_zicond_zicboz', mabi='lp64',
                      mmu_mode='sv39', privilege_modes=['m', 's', 'u'],
                      clock_frequency_hz=100000000, ram=[])
        groups, names = self.builder.smoke_selection(target)
        self.assertEqual(len(names), 23)
        self.assertEqual(len(names), len(set(names)))
        self.assertTrue(all('-p-' in name for name in names))
        self.assertIn('rv64ua-p-lrsc', names)
        self.assertIn('rv64mzicbo-p-zero', names)
        self.assertNotIn('rv64uc', groups)
        target['extensions'].append('c')
        _, compressed = self.builder.smoke_selection(target)
        self.assertEqual(set(compressed) - set(names), {'rv64uc-p-rvc'})
        target['xlen'] = 32
        with self.assertRaisesRegex(ValueError, 'RV64'):
            self.builder.smoke_selection(target)

    def test_full_isa_groups_follow_target_capabilities(self):
        target = program_target()
        target['extensions'] += ['a', 'f', 'd', 'c', 'zba', 'zicond']
        self.assertEqual(self.builder.isa_groups(target),
                         ['rv64ui', 'rv64um', 'rv64ua', 'rv64uf', 'rv64ud',
                          'rv64uc', 'rv64uzba', 'rv64uzicond'])
        target['xlen'] = 32
        with self.assertRaisesRegex(ValueError, 'RV64'):
            self.builder.isa_groups(target)

    def test_virtual_environment_requires_sv39_and_supervisor_user_modes(self):
        target = program_target()
        target['extensions'].append('v')
        self.assertFalse(self.builder.virtual_environment_enabled(target))
        target.update(mmu_mode='bare', privilege_modes=['m', 's', 'u'])
        self.assertFalse(self.builder.virtual_environment_enabled(target))
        target['mmu_mode'] = 'sv39'
        self.assertTrue(self.builder.virtual_environment_enabled(target))
        target['privilege_modes'] = ['m', 's']
        self.assertFalse(self.builder.virtual_environment_enabled(target))

    def test_makefrag_inventory_selects_virtual_tests_and_excludes_misalignment(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / 'Makefile').write_text(
                'rv64ui_p_tests = rv64ui-p-add rv64ui-p-ma_data\n'
                'rv64ui_v_tests = rv64ui-v-add rv64ui-v-ma_data\n')
            command = ['make', '--no-print-directory', '-s', '-f', str(SCRIPTS / 'isa.mk'),
                       f'src_dir={source}', 'program_groups=rv64ui',
                       'program_virtual_groups=rv64ui', 'program-manifest']
            names = subprocess.check_output(command, cwd=source, text=True).splitlines()
            self.assertEqual(names, ['rv64ui-p-add', 'rv64ui-v-add'])

    def test_benchmark_selection_follows_vector_capability_and_mode(self):
        target = program_target()
        self.assertEqual(self.builder.benchmark_selection(target, 'target'), self.builder.SCALAR_BENCHMARKS)
        target['extensions'].append('v')
        self.assertEqual(self.builder.benchmark_selection(target, 'target'),
                         self.builder.SCALAR_BENCHMARKS + self.builder.VECTOR_BENCHMARKS)
        self.assertEqual(self.builder.benchmark_selection(target, 'baseline'), self.builder.SCALAR_BENCHMARKS)

    def test_multihart_benchmark_selection_materializes_target_runtime(self):
        target = program_target('tiled-rv5stage-soc')
        target['harts'] = list(range(8))
        target['extensions'].append('a')
        self.assertEqual(self.builder.benchmark_selection(target, 'target', 'multihart'),
                         ('mt-matmul', 'mt-memcpy'))
        target['extensions'].append('d')
        self.assertEqual(self.builder.benchmark_selection(target, 'target', 'multihart'),
                         self.builder.MULTIHART_BENCHMARKS)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            (source / 'env').mkdir(parents=True)
            common = source / 'benchmarks/common'
            common.mkdir(parents=True)
            original = 'before\n  # for now, assume only 1 core\n  li a1, 1\nafter\n'
            (common / 'crt.S').write_text(original)
            for benchmark in self.builder.MULTIHART_BENCHMARKS:
                (source / 'benchmarks' / benchmark).mkdir()
            self.assertEqual((common / 'crt.S').read_text(), original)
            for hart_count in (2, 4, 8):
                with self.subTest(hart_count=hart_count):
                    generated = self.builder.materialize_multihart_source(
                        source, root / f'generated-{hart_count}', hart_count,
                        self.builder.MULTIHART_BENCHMARKS)
                    self.assertIn(f'li a1, {hart_count}', (generated / 'common/crt.S').read_text())
                    self.assertEqual((generated.parent / 'env').resolve(), (source / 'env').resolve())
                    for benchmark in self.builder.MULTIHART_BENCHMARKS:
                        self.assertEqual((generated / benchmark).resolve(),
                                         (source / 'benchmarks' / benchmark).resolve())

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
            target_path = root / 'target.json'
            target_path.write_text(json.dumps(program_target()))
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
                    '--output', str(output), '--compiler', sys.executable,
                    '--target', str(target_path), '--isa-selection', 'full']
            with patch.object(sys, 'argv', argv), patch.object(builder.subprocess, 'check_output', side_effect=check_output), \
                    patch.object(builder.subprocess, 'run', side_effect=run), \
                    patch.object(builder, 'check_elf_memory', return_value=[]):
                builder.main()
                builder.main()
                self.assertEqual(count, 1)
                manifest = json.loads((output / 'manifest.json').read_text())
                self.assertEqual(manifest['selection'], 'full')
                self.assertEqual(manifest['target'], program_target())
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
            target = dict(soc='mini-rv5stage-soc', xlen=64, harts=[0], extensions=['i'], march='rv64i', mabi='lp64',
                          clock_frequency_hz=100000000,
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
                    '--compiler', sys.executable, '--target', str(target_path),
                    '--isa-selection', 'smoke']
            with patch.object(sys, 'argv', argv), patch.object(builder.subprocess, 'check_output', side_effect=check_output), \
                    patch.object(builder.subprocess, 'run', side_effect=run):
                builder.main()
                with patch.object(builder, 'check_elf_memory', wraps=builder.check_elf_memory) as checked:
                    builder.main()
                    self.assertEqual(checked.call_count, len(names))
                self.assertEqual(len(builds), 1)
                manifest = json.loads((output / 'manifest.json').read_text())
                self.assertEqual(manifest['target'], target)
                self.assertEqual(manifest['selection'], 'smoke')
                target['soc'] = 'tiled-rv5stage-soc'
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
            target['extensions'].append('v')
            target['march'] = 'rv64imv_zba_zicond'
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
                    for name in builder.benchmark_selection(target, 'target'):
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
            self.assertIn('RISCV_MARCH=rv64imv_zba_zicond', make_commands[0])
            self.assertIn('RISCV_VMARCH=rv64imv_zba_zicond', make_commands[0])
            self.assertIn('RISCV_GCC_OPTS=' + builder.FLAGS + ' -mabi=lp64', make_commands[0])
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest['target'], target)
            self.assertEqual(manifest['target_fingerprint'], target_fingerprint(target))
            self.assertEqual(manifest['benchmark_mode'], 'target')
            self.assertEqual(manifest['benchmark_selection'], 'single-hart')
            self.assertEqual(manifest['compiler_arch'], 'normalized-arch')
            self.assertEqual(len(manifest['tests']), len(builder.SCALAR_BENCHMARKS + builder.VECTOR_BENCHMARKS))
            self.assertNotIn('vec-*', manifest['exclusions'])
            self.assertTrue((output / 'instruction-report.json').is_file())

    def test_multihart_benchmark_manifests_boot_selected_harts(self):
        builder = self.builder
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            (source / 'env/p').mkdir(parents=True)
            (source / 'env/p/link.ld').touch()
            common = source / 'benchmarks/common'
            common.mkdir(parents=True)
            (common / 'crt.S').write_text(
                'before\n  # for now, assume only 1 core\n  li a1, 1\nafter\n')
            (common / 'test.ld').touch()
            for benchmark in builder.MULTIHART_BENCHMARKS:
                (source / 'benchmarks' / benchmark).mkdir()
            target = program_target('tiled-rv5stage-soc')
            target['harts'] = list(range(8))
            target['extensions'].append('a')
            target['march'] = 'rv64ima'
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
                    for benchmark in builder.benchmark_selection(target, 'target', 'multihart'):
                        (kwargs['cwd'] / (benchmark + '.riscv')).write_bytes(b'ELF')
                return subprocess.CompletedProcess(command, 0)

            for build_index, hart_count in enumerate((2, 4, 8), start=1):
                with self.subTest(hart_count=hart_count):
                    output = root / f'output-{hart_count}'
                    argv = ['build.py', '--suite', 'benchmark', '--source', str(source),
                            '--output', str(output), '--compiler', sys.executable,
                            '--target', str(target_path), '--benchmark-selection', 'multihart',
                            '--benchmark-hart-count', str(hart_count)]
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
                    self.assertEqual(len(make_commands), build_index)
                    source_argument = next(argument for argument in make_commands[-1]
                                           if argument.startswith('src_dir='))
                    generated = Path(source_argument.removeprefix('src_dir='))
                    self.assertIn(f'li a1, {hart_count}', (generated / 'common/crt.S').read_text())
                    manifest = json.loads((output / 'manifest.json').read_text())
                    self.assertEqual(manifest['benchmark_selection'], 'multihart')
                    self.assertEqual(manifest['benchmark_hart_count'], hart_count)
                    self.assertEqual([test['name'] for test in manifest['tests']],
                                     [name + '.riscv' for name in
                                      builder.benchmark_selection(target, 'target', 'multihart')])
                    self.assertTrue(all(test['harts'] == list(range(hart_count))
                                        for test in manifest['tests']))
                    self.assertEqual(manifest['target']['harts'], target['harts'])
                    self.assertEqual(manifest['exclusions']['mt-vvadd'], 'Requires target extensions: d.')


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
