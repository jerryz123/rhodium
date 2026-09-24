# Tests litmus source selection, bare-metal adaptation, and model outcome mapping.
# SPDX-License-Identifier: Apache-2.0
import importlib.util
from pathlib import Path
import tempfile
import unittest


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
    def test_pinned_cases_keep_their_original_instructions_and_thread_counts(self):
        for name, hart_count, instruction in (
                ('MP', 2, 'sw x5,0(x6)'),
                ('SB', 2, 'lw x7,0(x8)'),
                ('IRIW+addrs', 4, 'add x10,x9,x7')):
            with self.subTest(name=name):
                parsed_name, registers, threads = BUILDER.parse_case(
                    SOURCE / 'tests' / PATHS[name])
                fields, states = BUILDER.model_states(SOURCE / 'model-results/herd.logs', name)
                asm = BUILDER.assembly(registers, threads, fields)
                self.assertEqual(parsed_name, name)
                self.assertEqual(len(threads), hart_count)
                self.assertIn(f'  {instruction}\n', asm)
                self.assertEqual(len(states), {'MP': 4, 'SB': 4, 'IRIW+addrs': 15}[name])
                self.assertIn(f'#define LITMUS_HARTS {hart_count}',
                              BUILDER.header(name, hart_count, fields, states, 10))

    def test_iriw_forbidden_outcome_is_absent_from_pinned_model(self):
        fields, states = BUILDER.model_states(SOURCE / 'model-results/herd.logs', 'IRIW+addrs')
        self.assertEqual(fields, [(1, 5), (1, 8), (3, 5), (3, 8)])
        self.assertNotIn([1, 0, 1, 0], states)

    def test_discovers_all_unambiguous_supported_cases_in_pinned_inventory(self):
        cases = BUILDER.discover_cases(SOURCE, SOURCE / 'model-results/herd.logs', 8)
        self.assertEqual(len(cases), 84)
        for name, path in PATHS.items():
            self.assertEqual(cases[name][0], SOURCE / 'tests' / path)
        self.assertEqual(sum(any(instruction.startswith('fence ')
                                 for thread in entry[2] for instruction in thread)
                             for entry in cases.values()), 72)

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

    def test_unknown_instruction_is_rejected_instead_of_silently_removed(self):
        source = (SOURCE / 'tests' / PATHS['MP']).read_text()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'MP.litmus'
            path.write_text(source.replace('sw x5,0(x6)', 'bogus x5,x6,x7', 1))
            with self.assertRaisesRegex(ValueError, 'unsupported instruction'):
                BUILDER.parse_case(path)

    def test_litmus_instruction_cannot_clobber_runtime_registers(self):
        source = (SOURCE / 'tests' / PATHS['MP']).read_text()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'MP.litmus'
            path.write_text(source.replace('lw x5,0(x6)', 'lw x1,0(x6)', 1))
            with self.assertRaisesRegex(ValueError, 'runtime-reserved register'):
                BUILDER.parse_case(path)


if __name__ == '__main__':
    unittest.main()
