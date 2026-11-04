# Checks litmus thread counts and allowed outcomes from the pinned memory model.
# SPDX-License-Identifier: Apache-2.0
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from sw.build.litmus_smoke import smoke_options, validate_smoke_manifest

SPEC = importlib.util.spec_from_file_location('litmus_build', ROOT / 'sw/build/build-litmus.py')
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)
SOURCE = ROOT / 'sw/litmus-tests-riscv'
PATHS = {
    'MP': 'non-mixed-size/BASIC_2_THREAD/MP.litmus',
    'SB': 'non-mixed-size/BASIC_2_THREAD/SB.litmus',
    'IRIW+addrs': 'non-mixed-size/SAFE/IRIW+addrs.litmus',
}


class LitmusModelTest(unittest.TestCase):
    def test_smoke_requires_every_case_once_with_its_requested_run_count(self):
        options = smoke_options()
        cases = [dict(name=name, litmus_min_samples=options['runs']) for name in reversed(options['tests'])]
        manifest = dict(build_spec=dict(options=options), runs=options['runs'], tests=cases)
        validate_smoke_manifest(manifest, options)

        for reason, changes in (
            ('missing', dict(tests=cases[1:])),
            ('duplicate', dict(tests=cases[1:] + [cases[1]])),
            ('unexpected', dict(tests=[cases[0] | dict(name='unexpected'), *cases[1:]])),
            ('wrong runs', dict(runs=options['runs'] + 1)),
            ('wrong samples', dict(tests=[cases[0] | dict(litmus_min_samples=0), *cases[1:]])),
        ):
            with self.subTest(reason=reason), self.assertRaises(ValueError):
                validate_smoke_manifest(manifest | changes, options)
        truncated = options | dict(tests=options['tests'][:-1])
        with self.assertRaises(ValueError):
            validate_smoke_manifest(manifest | dict(build_spec=dict(options=truncated)), truncated)

    def test_iriw_forbidden_outcome_is_absent_from_pinned_model(self):
        cases = BUILDER.discover_litmus7_cases(SOURCE, SOURCE / 'model-results/herd.logs', 4)
        self.assertEqual(cases['IRIW+addrs'][1], 4)
        self.assertEqual(len(cases['IRIW+addrs'][2]), 15)
        self.assertNotIn('1:x5=1; 1:x8=0; 3:x5=1; 3:x8=0;', cases['IRIW+addrs'][2])

    def test_litmus7_discovery_uses_source_thread_tables_and_model_states(self):
        cases = BUILDER.discover_litmus7_cases(SOURCE, SOURCE / 'model-results/herd.logs', 4)
        self.assertEqual(cases['MP'][1], 2)
        self.assertEqual(cases['LB+ctrls'][1], 2)
        self.assertEqual(cases['CoRW1'][2], ['0:x5=0; x=1;'])
        self.assertEqual(cases['MP'][0], SOURCE / 'tests' / PATHS['MP'])
        supported = BUILDER.supported_litmus7_cases(cases, [])
        self.assertIn('MP', supported)
        self.assertNotIn('2+2W+[rf-addr-fr]+poprl', supported)
        self.assertEqual(BUILDER.supported_litmus7_cases(cases, ['zalasr']), cases)


if __name__ == '__main__':
    unittest.main()
