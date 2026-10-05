# Tests target-software descriptors and ELF builders across supported workload suites.
# SPDX-License-Identifier: Apache-2.0
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

BUILD_SCRIPTS = Path(__file__).resolve().parents[1] / 'build'


def program_target(soc='single-core-rv5stage-soc'):
    return dict(soc=soc, xlen=64, harts=[0], extensions=['i', 'm'], march='rv64im',
                mabi='lp64', clock_frequency_hz=100000000,
                ram=[dict(base=0x80000000, size=0x10000)])


def target_fingerprint(target):
    return hashlib.sha256(json.dumps(target, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


class ProgramTargetTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('program_target', BUILD_SCRIPTS / 'program_target.py')
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


    def test_shared_build_spec_ignores_config_and_microarchitecture_identity(self):
        first = program_target('simple-rv5stage-rva23')
        second = first | dict(soc='simple-spike-rva23', resolved_configuration={'core': 'spike'},
                              configuration_fingerprint='different')
        for suite in ('isa', 'benchmark', 'coremark', 'coremark_scalar', 'embench', 'bringup', 'litmus'):
            with self.subTest(suite=suite):
                self.assertEqual(self.target.elf_build_metadata(first, suite),
                                 self.target.elf_build_metadata(second, suite))
        self.assertNotEqual(self.target.target_fingerprint(first), self.target.target_fingerprint(second))

    def test_shared_build_spec_tracks_binary_and_inventory_inputs(self):
        target = program_target()
        for change in (dict(march='rv64ima'), dict(mabi='lp64d'),
                       dict(ram=[dict(base=0x80000000, size=0x20000)])):
            for suite in ('isa', 'benchmark', 'coremark', 'embench', 'bringup'):
                with self.subTest(change=change, suite=suite):
                    self.assertNotEqual(self.target.elf_build_spec(target, suite),
                                        self.target.elf_build_spec(target | change, suite))
        clock = target | dict(clock_frequency_hz=200000000)
        self.assertNotEqual(self.target.elf_build_spec(target, 'coremark'),
                            self.target.elf_build_spec(clock, 'coremark'))
        self.assertEqual(self.target.elf_build_spec(target, 'bringup'),
                         self.target.elf_build_spec(clock, 'bringup'))
        sv39 = target | dict(mmu_mode='sv39', privilege_modes=['m', 's', 'u'])
        self.assertNotEqual(self.target.elf_build_spec(target, 'isa'),
                            self.target.elf_build_spec(sv39, 'isa'))
        for options in (dict(isa_selection='full'), dict(isa_selection='smoke')):
            self.assertNotEqual(self.target.elf_build_spec(target, 'isa'),
                                self.target.elf_build_spec(target, 'isa', options))

    def test_litmus_reuse_tracks_harts_extensions_clock_and_workload_options(self):
        target = program_target()
        spec = self.target.elf_build_spec(target, 'litmus', dict(runs=1, tests=['MP'], generator='litmus7'))
        for change in (dict(harts=[0, 1]), dict(extensions=['i', 'm', 'a']), dict(clock_frequency_hz=200000000)):
            self.assertNotEqual(spec, self.target.elf_build_spec(target | change, 'litmus', spec['options']))
        for options in (dict(runs=2, tests=['MP'], generator='litmus7'),
                        dict(runs=1, tests=['SB'], generator='litmus7')):
            self.assertNotEqual(spec, self.target.elf_build_spec(target, 'litmus', options))


class SharedELFBindingTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('elf_bind', BUILD_SCRIPTS / 'bind.py')
        self.binder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.binder)
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.target = program_target('simple-rv5stage-rva23')
        base = self.target['ram'][0]['base']
        elf = self.root / 'test.elf'
        header = b'\x7fELF\x02\x01\x01' + bytes(9)
        header += struct.pack('<HHIQQQIHHHHHH', 2, 243, 1, base, 64, 0, 0, 64, 56, 1, 0, 0, 0)
        segment = struct.pack('<IIQQQQQQ', 1, 5, 120, base, base, 1, 128, 1)
        elf.write_bytes(header + segment + b'\x00')
        build_spec = self.binder.elf_build_spec(self.target, 'isa')
        self.manifest = dict(suite='isa', target=self.target, target_fingerprint=target_fingerprint(self.target),
                             build_spec=build_spec, build_spec_fingerprint=target_fingerprint(build_spec),
                             tests=[dict(name='test', elf='test.elf', sha256=hashlib.sha256(elf.read_bytes()).hexdigest())])
        self.path = self.root / 'manifest.json'
        self.output = self.root / 'run-manifest.json'
        self.write_manifest()

    def write_manifest(self):
        self.path.write_text(json.dumps(self.manifest))

    def test_binds_without_changing_elf_or_original_manifest(self):
        original = self.path.read_bytes()
        target = self.target | dict(soc='simple-spike-rva23')
        self.binder.bind(self.path, target, self.output)
        manifest = json.loads(self.output.read_text())
        self.assertEqual(manifest['target'], target)
        self.assertEqual(manifest['target_fingerprint'], target_fingerprint(target))
        self.assertEqual(manifest['build_target'], self.target)
        self.assertEqual(manifest['tests'][0]['sha256'], self.manifest['tests'][0]['sha256'])
        self.assertEqual(manifest['tests'][0]['load_segments'], [dict(address=0x80000000, memory_bytes=128)])
        self.assertEqual(self.path.read_bytes(), original)

    def test_rejects_incompatible_target_or_corrupt_provenance_and_elf(self):
        for target in (self.target | dict(march='rv64ima'), self.target | dict(harts=[1])):
            with self.assertRaises(ValueError):
                self.binder.bind(self.path, target, self.output)
            self.assertFalse(self.output.exists())
        self.manifest['build_spec_fingerprint'] = 'bad'
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'build specification'):
            self.binder.bind(self.path, self.target, self.output)
        self.manifest['build_spec_fingerprint'] = target_fingerprint(self.manifest['build_spec'])
        self.write_manifest()
        (self.root / 'test.elf').write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            self.binder.bind(self.path, self.target, self.output)
        self.assertFalse(self.output.exists())

    def test_rechecks_elf_width_memory_and_safe_paths(self):
        elf = self.root / 'test.elf'
        original = elf.read_bytes()
        for data, message in ((original[:4] + b'\x01' + original[5:], 'XLEN'),
                              (original[:104] + struct.pack('<Q', 0x10001) + original[112:], 'exceeds target RAM')):
            elf.write_bytes(data)
            self.manifest['tests'][0]['sha256'] = hashlib.sha256(data).hexdigest()
            self.write_manifest()
            with self.assertRaisesRegex(ValueError, message):
                self.binder.bind(self.path, self.target, self.output)
        self.manifest['tests'][0]['elf'] = '../test.elf'
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'invalid shared ELF path'):
            self.binder.bind(self.path, self.target, self.output)

    def test_only_upstream_benchmarks_allow_entry_without_executable_segment_flag(self):
        elf = self.root / 'test.elf'
        data = bytearray(elf.read_bytes())
        struct.pack_into('<I', data, 68, 0)
        elf.write_bytes(data)
        self.manifest['tests'][0]['sha256'] = hashlib.sha256(data).hexdigest()
        for suite in ('benchmark', 'isa', 'coremark', 'coremark_scalar', 'embench', 'bringup', 'litmus'):
            with self.subTest(suite=suite):
                spec = self.binder.elf_build_spec(self.target, suite)
                self.manifest.update(suite=suite, build_spec=spec,
                                     build_spec_fingerprint=target_fingerprint(spec))
                self.write_manifest()
                self.output.unlink(missing_ok=True)
                if suite == 'benchmark':
                    for soc in ('simple-rv5stage-rva23', 'simple-spike-rva23'):
                        target = self.target | dict(soc=soc)
                        self.binder.bind(self.path, target, self.output)
                        bound = json.loads(self.output.read_text())
                        self.assertEqual(bound['target'], target)
                        self.assertEqual(bound['tests'][0]['load_segments'],
                                         [dict(address=0x80000000, memory_bytes=128)])
                        self.assertEqual(elf.read_bytes(), data)
                else:
                    with self.assertRaisesRegex(ValueError, 'executable RAM segment'):
                        self.binder.bind(self.path, self.target, self.output)
                    self.assertFalse(self.output.exists())

    def test_benchmark_binding_still_checks_loaded_entry_ram_and_boot_harts(self):
        spec = self.binder.elf_build_spec(self.target, 'benchmark')
        self.manifest.update(suite='benchmark', build_spec=spec,
                             build_spec_fingerprint=target_fingerprint(spec))
        elf = self.root / 'test.elf'
        original = bytearray(elf.read_bytes())
        struct.pack_into('<I', original, 68, 0)
        for offset, value, error in ((24, 0x80000080, 'entry is not in a RAM load segment'),
                                     (104, 0x10001, 'exceeds target RAM')):
            with self.subTest(error=error):
                data = bytearray(original)
                struct.pack_into('<Q', data, offset, value)
                elf.write_bytes(data)
                self.manifest['tests'][0]['sha256'] = hashlib.sha256(data).hexdigest()
                self.write_manifest()
                with self.assertRaisesRegex(ValueError, error):
                    self.binder.bind(self.path, self.target, self.output)
                self.assertFalse(self.output.exists())
        elf.write_bytes(original)
        self.manifest['tests'][0].update(sha256=hashlib.sha256(original).hexdigest(), harts=[1])
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'boot harts'):
            self.binder.bind(self.path, self.target, self.output)
        self.assertFalse(self.output.exists())


class CoreMarkBuildTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('coremark_build', BUILD_SCRIPTS / 'build-coremark.py')
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
        spec = importlib.util.spec_from_file_location('embench_build', BUILD_SCRIPTS / 'build-embench.py')
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
        spec = importlib.util.spec_from_file_location('program_build', BUILD_SCRIPTS / 'build.py')
        self.builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.builder)

    def test_native_run_targets_use_existing_manifests_and_preserve_limits(self):
        for suite in ('isa', 'benchmark', 'coremark', 'coremark_scalar', 'embench', 'bringup'):
            with self.subTest(suite=suite):
                result = subprocess.run(['make', '-n', '-C', str(BUILD_SCRIPTS.parents[1] / 'sims'),
                                         suite + '-run', 'SOC=simple-spike-rva23',
                                         'PREBUILT_SIMULATOR=/tmp/shared-simulator/VTestDriver',
                                         'PROGRAM_MANIFEST=/tmp/shared-suite/run-manifest.json'],
                                        check=True, capture_output=True, text=True)
                self.assertIn('program-test/run.py --manifest "/tmp/shared-suite/run-manifest.json"', result.stdout)
                self.assertNotIn('sw/build/build', result.stdout)
                self.assertNotIn('tools/run-racket.sh', result.stdout)
                self.assertIn('--timeout ' + ('360' if suite == 'bringup' else '300'), result.stdout)
                self.assertIn('--max-cycles ' + ('10000000' if suite == 'isa' else '100000000'), result.stdout)

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
        self.assertNotIn('rv64ui-p-ma_data', names)
        self.assertNotIn('rv64uc', groups)
        target['extensions'].append('c')
        _, compressed = self.builder.smoke_selection(target)
        self.assertEqual(set(compressed) - set(names), {'rv64uc-p-rvc'})
        target['xlen'] = 32
        groups, names = self.builder.smoke_selection(target)
        self.assertEqual(len(names), 23)
        self.assertTrue(all(name.startswith('rv32') for name in names))
        self.assertIn('rv32ui-p-lw', names)
        self.assertIn('rv32ui-p-sw', names)
        self.assertIn('rv32ua-p-amoadd_w', names)
        target['extensions'].append('zicclsm')
        _, misaligned = self.builder.smoke_selection(target)
        self.assertIn('rv32ui-p-ma_data', misaligned)
        self.assertIn('rv32uc-p-rvc', names)
        self.assertNotIn('rv32mzicbo', groups)

    def test_full_isa_groups_follow_target_capabilities(self):
        target = program_target()
        target['extensions'] += ['a', 'f', 'd', 'c', 'zba', 'zicond']
        self.assertEqual(self.builder.isa_groups(target),
                         ['rv64ui', 'rv64um', 'rv64ua', 'rv64uf', 'rv64ud',
                          'rv64uc', 'rv64uzba', 'rv64uzicond'])
        target['xlen'] = 32
        self.assertEqual(self.builder.isa_groups(target),
                         ['rv32ui', 'rv32um', 'rv32ua', 'rv32uf', 'rv32ud',
                          'rv32uc', 'rv32uzba', 'rv32uzicond'])

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

    def test_makefrag_inventory_selects_misalignment_when_supported(self):
        for xlen in (32, 64):
            with self.subTest(xlen=xlen), tempfile.TemporaryDirectory() as directory:
                source = Path(directory)
                group = f'rv{xlen}ui'
                (source / 'Makefile').write_text(
                    f'{group}_p_tests = {group}-p-add {group}-p-ma_data\n'
                    f'{group}_v_tests = {group}-v-add {group}-v-ma_data\n')
                command = ['make', '--no-print-directory', '-s', '-f', str(BUILD_SCRIPTS / 'isa.mk'),
                           f'XLEN={xlen}', f'src_dir={source}', f'program_groups={group}',
                           f'program_virtual_groups={group}', 'program-manifest']
                excluded = subprocess.check_output(command, cwd=source, text=True).splitlines()
                self.assertEqual(excluded, [f'{group}-p-add', f'{group}-v-add'])
                enabled = subprocess.check_output(command[:-1] + ['program_misaligned=1', 'program-manifest'], cwd=source, text=True).splitlines()
                self.assertEqual(enabled, [f'{group}-p-add', f'{group}-p-ma_data', f'{group}-v-add', f'{group}-v-ma_data'])

    def test_rv32_elf_footprint_checks_bss_entry_and_program_headers(self):
        with tempfile.TemporaryDirectory() as directory:
            elf = Path(directory) / 'test.elf'
            base = 0x80000000
            ram = [dict(base=base, size=0x8000)]

            def write_elf(memsz=0x8000, entry=base, flags=5, phsize=32):
                header = b'\x7fELF\x01\x01\x01' + bytes(9)
                header += struct.pack('<HHIIIIIHHHHHH', 2, 243, 1, entry, 52, 0, 0, 52, phsize, 1, 0, 0, 0)
                segment = struct.pack('<IIIIIIII', 1, 84, base, base, 1, memsz, flags, 1)
                elf.write_bytes(header + segment + b'\x00')

            write_elf()
            self.assertEqual(self.builder.check_elf_memory(elf, ram), [dict(address=base, memory_bytes=0x8000)])
            for values in (dict(memsz=0x8001), dict(entry=base + 0x8000), dict(flags=0), dict(phsize=56)):
                write_elf(**values)
                with self.assertRaises(ValueError):
                    self.builder.check_elf_memory(elf, ram)
            elf.write_bytes(b'\x7fELF\x01\x01\x01' + bytes(9))
            with self.assertRaises(ValueError):
                self.builder.check_elf_memory(elf, ram)

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
            original_exit = 'before\nvoid exit(int code)\n{\n  tohost_exit(code);\n}\nafter\n'
            (common / 'syscalls.c').write_text(original_exit)
            for benchmark in self.builder.MULTIHART_BENCHMARKS:
                (source / 'benchmarks' / benchmark).mkdir()
            self.assertEqual((common / 'crt.S').read_text(), original)
            self.assertEqual((common / 'syscalls.c').read_text(), original_exit)
            for hart_count in (2, 4, 8):
                with self.subTest(hart_count=hart_count):
                    generated = self.builder.materialize_multihart_source(
                        source, root / f'generated-{hart_count}', hart_count,
                        self.builder.MULTIHART_BENCHMARKS)
                    self.assertIn(f'li a1, {hart_count}', (generated / 'common/crt.S').read_text())
                    self.assertIn(f'if (completed == {hart_count})',
                                  (generated / 'common/syscalls.c').read_text())
                    self.assertIn('rhodium_exit_error', (generated / 'common/syscalls.c').read_text())
                    self.assertEqual((common / 'syscalls.c').read_text(), original_exit)
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
        spec = importlib.util.spec_from_file_location('program_build', BUILD_SCRIPTS / 'build.py')
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
                target_path.write_text(json.dumps(program_target('simple-spike-rva23')))
                builder.main()
                shared = json.loads((output / 'manifest.json').read_text())
                self.assertEqual(count, 1)
                self.assertEqual(shared['cache_key'], manifest['cache_key'])
                self.assertEqual(shared['target']['soc'], 'simple-spike-rva23')
                self.assertNotEqual(shared['target_fingerprint'], manifest['target_fingerprint'])
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
            (common / 'syscalls.c').write_text(
                'before\nvoid exit(int code)\n{\n  tohost_exit(code);\n}\nafter\n')
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
                    self.assertIn(f'if (completed == {hart_count})',
                                  (generated / 'common/syscalls.c').read_text())
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
