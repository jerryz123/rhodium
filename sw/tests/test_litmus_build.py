# Tests litmus selection, bare-metal adaptation, stream binding, and model outcomes.
# SPDX-License-Identifier: Apache-2.0
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('litmus_build', ROOT / 'sw/build/build-litmus.py')
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)
SOURCE = ROOT / 'sw/litmus-tests-riscv'
PATHS = {
    'MP': 'non-mixed-size/BASIC_2_THREAD/MP.litmus',
    'SB': 'non-mixed-size/BASIC_2_THREAD/SB.litmus',
    'IRIW+addrs': 'non-mixed-size/SAFE/IRIW+addrs.litmus',
}


class LitmusBuildTest(unittest.TestCase):
    def test_selected_litmus7_builds_reuse_checksums_and_keep_failure_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tool = root / 'litmus7'
            tool.write_bytes(b'pinned litmus7')
            target = dict(soc='tiled-test', xlen=64, harts=list(range(8)),
                          extensions=['i', 'm', 'a'], march='rv64ima', mabi='lp64',
                          clock_frequency_hz=100000000,
                          ram=[dict(base=0x80000000, size=0x1000000)])
            target_path = root / 'target.json'
            target_path.write_text(json.dumps(target))
            selection = root / 'cases.txt'
            selection.write_text('# Explicit selection\nMP\nSB\n')
            commands = []
            fail_sb = False

            def check_output(command, **kwargs):
                if command[0] == 'git':
                    return 'pinned-revision\n'
                if '--version' in command or '-version' in command:
                    return 'pinned-tool-version\n'
                raise AssertionError(command)

            def run(command, **kwargs):
                if command[0] == 'git':
                    return subprocess.CompletedProcess(command, 0)
                commands.append(command)
                output = Path(command[command.index('-o') + 1])
                if command[0] == str(tool):
                    name = Path(command[-1]).stem
                    if fail_sb and name == 'SB':
                        return subprocess.CompletedProcess(command, 1)
                    (output / f'{name}.c').write_text('''typedef struct {
  volatile int c,sense;
  int n ;
} sense_t;
void init(sense_t *p, int n) {
  p->n = p->c = n;
  p->sense = 0;
}
__attribute__ ((noinline)) static void barrier_wait(sense_t *p) {
  int rem = __sync_add_and_fetch(&p->c,-1) ;
}
void output(FILE *out) {
  fflush(out);
}
''')
                else:
                    output.write_bytes(b'compiled ELF')
                return subprocess.CompletedProcess(command, 0)

            args = ['build-litmus.py', '--source', str(SOURCE), '--runtime',
                    str(ROOT / 'sw/litmus-riscv-baremetal'), '--riscv-tests',
                    str(ROOT / 'sw/riscv-isa-tests'), '--target', str(target_path),
                    '--compiler', 'test-gcc', '--litmus7', str(tool), '--runs', '3']
            with (patch.object(BUILDER.shutil, 'which', side_effect=lambda value: value),
                  patch.object(BUILDER.subprocess, 'check_output', side_effect=check_output),
                  patch.object(BUILDER.subprocess, 'run', side_effect=run),
                  patch.object(BUILDER, 'probe_compiler', return_value='rv64ima'),
                  patch.object(BUILDER, 'readelf_for', return_value='test-readelf'),
                  patch.object(BUILDER, 'elf_architecture', return_value='rv64ima'),
                  patch.object(BUILDER, 'check_elf_memory', return_value=[]),
                  patch('sys.stdout', new_callable=io.StringIO)):
                build_args = args + ['--output', str(root / 'built'), '--tests-file', str(selection)]
                with patch.object(sys, 'argv', build_args):
                    BUILDER.main()
                    manifest = json.loads((root / 'built/manifest.json').read_text())
                    self.assertEqual([test['name'] for test in manifest['tests']], ['MP', 'SB'])
                    self.assertEqual(manifest['generator'], 'litmus7')
                    self.assertEqual(manifest['build_failures'], [])
                    for test in manifest['tests']:
                        self.assertEqual(test['harts'], [0, 1])
                        self.assertEqual(test['litmus_min_samples'], 3)
                        self.assertEqual(len(test['litmus_allowed_states']), 4)
                    self.assertEqual(len(commands), 6)
                    BUILDER.main()
                    self.assertEqual(len(commands), 6)
                    elf = root / 'built' / manifest['tests'][0]['elf']
                    elf.write_bytes(b'changed ELF')
                    BUILDER.main()
                    self.assertEqual(len(commands), 9)
                fail_sb = True
                with patch.object(sys, 'argv', args + ['--output', str(root / 'partial'),
                                                       '--tests', 'MP,SB', '--keep-going']):
                    with self.assertRaises(SystemExit) as failure:
                        BUILDER.main()
                self.assertEqual(failure.exception.code, 1)
                partial = json.loads((root / 'partial/manifest.json').read_text())
                self.assertEqual([test['name'] for test in partial['tests']], ['MP'])
                self.assertEqual(partial['attempted_cases'], 2)
                self.assertEqual(partial['build_failures'],
                                 [dict(name='SB', source='tests/' + PATHS['SB'], stage='generation')])

    def test_iriw_forbidden_outcome_is_absent_from_pinned_model(self):
        cases = BUILDER.discover_litmus7_cases(SOURCE, SOURCE / 'model-results/herd.logs', 8)
        self.assertEqual(cases['IRIW+addrs'][1], 4)
        self.assertEqual(len(cases['IRIW+addrs'][2]), 15)
        self.assertNotIn('1:x5=1; 1:x8=0; 3:x5=1; 3:x8=0;', cases['IRIW+addrs'][2])

    def test_litmus7_discovery_uses_source_thread_tables_and_model_states(self):
        cases = BUILDER.discover_litmus7_cases(SOURCE, SOURCE / 'model-results/herd.logs', 8)
        self.assertEqual(len(cases), 6521)
        self.assertEqual(cases['MP'][1], 2)
        self.assertEqual(cases['LB+ctrls'][1], 2)
        self.assertEqual(cases['CoRW1'][2], ['0:x5=0; x=1;'])
        self.assertEqual(cases['MP'][0], SOURCE / 'tests' / PATHS['MP'])
        supported = BUILDER.supported_litmus7_cases(cases, [])
        self.assertEqual(len(supported), 3443)
        self.assertIn('MP', supported)
        self.assertNotIn('2+2W+[rf-addr-fr]+poprl', supported)
        self.assertEqual(BUILDER.supported_litmus7_cases(cases, ['zalasr']), cases)

    def test_smoke_names_are_unique_supported_cases_across_hart_counts(self):
        names = [line.strip() for line in (ROOT / 'sw/litmus-riscv-baremetal/smoke-cases.txt').read_text().splitlines()
                 if line.strip() and not line.lstrip().startswith('#')]
        cases = BUILDER.supported_litmus7_cases(
            BUILDER.discover_litmus7_cases(SOURCE, SOURCE / 'model-results/herd.logs', 8), [])
        self.assertEqual(len(names), len(set(names)))
        self.assertTrue(names)
        self.assertFalse(set(names) - set(cases))
        self.assertEqual({cases[name][1] for name in names}, {2, 3, 4})

    def test_litmus7_support_code_uses_only_scalar_target_extensions(self):
        extensions = ['i', 'm', 'a', 'f', 'd', 'c', 'v', 'zicsr', 'zifencei', 'zicond']
        self.assertEqual(BUILDER.litmus7_codegen_march(extensions),
                         'rv64imafdc_zicsr_zifencei')
        self.assertEqual(BUILDER.litmus7_codegen_march(extensions + ['zalasr']),
                         'rv64imafdc_zicsr_zifencei_zalasr')

    def test_litmus7_runtime_adaptation_is_checked_and_limited(self):
        generated = '''typedef struct {
  volatile int c,sense;
  int n ;
} sense_t;
static void barrier_init(sense_t *p,int n) {
  p->n = p->c = n;
  p->sense = 0;
}
__attribute__ ((noinline)) static void barrier_wait(sense_t *p) {
  int rem = __sync_add_and_fetch(&p->c,-1) ;
}
void output(FILE *out) {
  fflush(out);
}
const char *instruction = "amoadd.w";
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'generated.c'
            path.write_text(generated)
            BUILDER.adapt_litmus7_runtime(path)
            adapted = path.read_text()
            self.assertIn('litmus_baremetal_barrier_init(p,n)', adapted)
            self.assertIn('litmus_baremetal_barrier_wait(p)', adapted)
            self.assertNotIn('__sync_add_and_fetch', adapted)
            self.assertNotIn('fflush(out)', adapted)
            self.assertIn('"amoadd.w"', adapted)
            path.write_text(generated.replace('__sync_add_and_fetch', '__atomic_fetch_add'))
            with self.assertRaisesRegex(ValueError, 'unrecognized litmus7 barrier template'):
                BUILDER.adapt_litmus7_runtime(path)

    def test_litmus7_streams_do_not_reference_newlib_stdio_state(self):
        compiler = shutil.which('riscv64-unknown-elf-gcc')
        nm = shutil.which('riscv64-unknown-elf-nm')
        if not compiler or not nm:
            self.skipTest('RISC-V GCC and nm are required for the bare-metal stream check')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'streams.c'
            object_file = Path(directory) / 'streams.o'
            source.write_text('#include <stdio.h>\n'
                              'FILE *litmus_out(void) { return stdout; }\n'
                              'int litmus_error(void) { return fprintf(stderr, "%d", 1); }\n')
            subprocess.run([compiler, '-O2', '-fno-builtin-fprintf', '-include',
                            str(ROOT / 'sw/litmus-riscv-baremetal/litmus7-streams.h'),
                            '-c', str(source), '-o', str(object_file)], check=True)
            undefined = subprocess.check_output([nm, '-u', str(object_file)], text=True)
            self.assertNotIn('_impure_ptr', undefined)
            self.assertIn('litmus_baremetal_stdout_stream', undefined)
            self.assertIn('litmus_baremetal_stderr_stream', undefined)

if __name__ == '__main__':
    unittest.main()
