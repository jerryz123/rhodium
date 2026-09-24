#!/usr/bin/env python3
# Checks capability-based OpenSBI layout selection and RAM reservations.
# SPDX-License-Identifier: Apache-2.0
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / 'build/opensbi.py'


def target(**updates):
    value = {
        'soc': 'test-soc',
        'xlen': 64,
        'extensions': ['i', 'm', 'a', 'c', 'zicsr', 'zifencei', 'zicntr'],
        'march': 'rv64imac_zicsr_zifencei_zicntr',
        'mabi': 'lp64',
        'clock_frequency_hz': 100000000,
        'harts': [0],
        'boot': {'payload_address': 0x80000000},
        'ram': [{'base': 0x80000000, 'size': 0x40000000}],
    }
    value.update(updates)
    return value


class OpenSbiTargetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location('opensbi_adapter', SCRIPT)
        cls.adapter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.adapter)

    def test_layout_comes_from_target_capabilities_and_ram(self):
        layout = self.adapter.target_layout(target())
        self.assertEqual(layout['firmware_address'], 0x80000000)
        self.assertEqual(layout['firmware_link_address'], 0)
        self.assertEqual(layout['next_stage_address'], 0x80200000)
        self.assertEqual(layout['fdt_address'], 0xbfff0000)
        self.assertEqual(layout['fdt_reservation_size'], 0x10000)
        self.assertEqual((layout['ram_base'], layout['ram_size']), (0x80000000, 0x40000000))
        self.assertEqual(layout['opensbi_isa'], 'rv64imac_zicsr_zifencei')

    def test_layout_rejects_missing_capabilities(self):
        with self.assertRaisesRegex(ValueError, r"missing \['a'\]"):
            self.adapter.target_layout(target(extensions=['i', 'm', 'zicsr', 'zifencei', 'zicntr']))
        with self.assertRaisesRegex(ValueError, 'bootable hart 0'):
            self.adapter.target_layout(target(harts=[0, 1]))

    def test_layout_rejects_insufficient_or_misplaced_ram(self):
        with self.assertRaisesRegex(ValueError, 'cannot hold'):
            self.adapter.target_layout(target(ram=[{'base': 0x80000000, 'size': 0x10000}]))
        with self.assertRaisesRegex(ValueError, 'outside target RAM'):
            self.adapter.target_layout(target(ram=[{'base': 0x90000000, 'size': 0x10000000}]))

    def test_fdt_validation_checks_header_size_and_reservation(self):
        layout = self.adapter.target_layout(target())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'platform.dtb'
            valid = struct.pack('>II', 0xd00dfeed, 8)
            path.write_bytes(valid)
            self.assertEqual(self.adapter.validate_fdt(layout, path), valid)

            path.write_bytes(b'not a dtb')
            with self.assertRaisesRegex(ValueError, 'not a flattened device tree'):
                self.adapter.validate_fdt(layout, path)

            path.write_bytes(struct.pack('>II', 0xd00dfeed, 9))
            with self.assertRaisesRegex(ValueError, 'invalid total size'):
                self.adapter.validate_fdt(layout, path)

            path.write_bytes(struct.pack('>II', 0xd00dfeed, 8))
            layout['fdt_reservation_size'] = 7
            with self.assertRaisesRegex(ValueError, 'exceeds its reserved RAM range'):
                self.adapter.validate_fdt(layout, path)


if __name__ == '__main__':
    unittest.main()
