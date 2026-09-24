# Checks Bringup-Bench source inventory and output-oracle adapter contracts.
# SPDX-License-Identifier: Apache-2.0
import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'program-test/build-bringup-bench.py'


def load_adapter():
    spec = importlib.util.spec_from_file_location('build_bringup_bench', SCRIPT)
    adapter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(adapter)
    return adapter


def fixture(root):
    (root / 'common').mkdir()
    (root / 'common/libmin_success.c').write_text('void success(void) {}\n')
    (root / 'tiny-NN').mkdir()
    (root / 'tiny-NN/Makefile').write_text(
        'LOCAL_CFLAGS=\nLOCAL_LIBS=\nLOCAL_OBJS=tiny-NN.o\nPROG=tiny-NN\n')
    (root / 'tiny-NN/tiny-NN.c').write_text('int main(void) { return 0; }\n')
    (root / 'tiny-NN/tiny-NN.hash').write_text('** hashval = 0x0123456789abcdef\n')
    (root / 'Makefile').write_text(
        'BMARKS = tiny-NN\n__LIBMIN_SRCS = libmin_success.c\nLIBMIN_SRCS = generated\n')


class BringupAdapterTest(unittest.TestCase):
    def setUp(self):
        self.adapter = load_adapter()

    def test_inventory_accepts_upstream_capitalized_name_and_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture(root)
            common, benchmarks = self.adapter.upstream_inventory(root)
            self.assertEqual([path.name for path in common], ['libmin_success.c'])
            self.assertEqual(list(benchmarks), ['tiny-NN'])
            self.assertEqual(benchmarks['tiny-NN']['expected_hash'], 0x0123456789abcdef)

    def test_inventory_rejects_missing_reference_and_recipe_divergence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture(root)
            (root / 'tiny-NN/tiny-NN.hash').write_text('unverified\n')
            with self.assertRaisesRegex(ValueError, 'output hash'):
                self.adapter.upstream_inventory(root)
            (root / 'tiny-NN/tiny-NN.hash').write_text('** hashval = 0x0123456789abcdef\n')
            (root / 'tiny-NN/Makefile').write_text(
                'LOCAL_CFLAGS=-DOTHER_TARGET\nLOCAL_LIBS=\nLOCAL_OBJS=tiny-NN.o\nPROG=tiny-NN\n')
            with self.assertRaisesRegex(ValueError, 'unsupported upstream build recipe'):
                self.adapter.upstream_inventory(root)

    def test_header_port_has_exact_upstream_anchor(self):
        header = 'defined(TARGET_HASPIKE) || defined(TARGET_CVA6_RV64)'
        self.assertIn('defined(TARGET_RHODIUM)', self.adapter.rhodium_header(header))
        with self.assertRaisesRegex(ValueError, 'target header changed'):
            self.adapter.rhodium_header('different header')

    def test_single_profile_selects_all_and_overrides_bounded_hashes(self):
        inventory = {name: {'expected_hash': index + 1}
                     for index, name in enumerate(self.adapter.BOUNDED_HASHES)}
        inventory['tiny-NN'] = {'expected_hash': 99}
        self.assertEqual(self.adapter.selected_names(inventory), list(inventory))
        self.assertEqual(self.adapter.selected_names(inventory, 'rho-factor,checkers'),
                         ['checkers', 'rho-factor'])
        self.assertEqual(self.adapter.expected_hash(inventory, 'tiny-NN'), 99)
        self.assertEqual(self.adapter.expected_hash(inventory, 'checkers'),
                         self.adapter.BOUNDED_HASHES['checkers'])
        with self.assertRaisesRegex(ValueError, 'unknown or duplicate'):
            self.adapter.selected_names(inventory, 'tiny-NN,tiny-NN')
        with self.assertRaisesRegex(ValueError, 'missing from upstream'):
            self.adapter.selected_names({'tiny-NN': {}})


if __name__ == '__main__':
    unittest.main()
