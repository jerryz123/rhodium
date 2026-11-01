# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'build'))
from linux import buildroot_architecture, linux_layout
from test_opensbi_build import target


def linux_target(**updates):
    value = target(extensions=['i', 'm', 'a', 'f', 'd', 'c', 'v', 'zba', 'zbb', 'zbs',
                               'zicsr', 'zifencei', 'zicntr'],
                   march='rv64imafdcv_zba_zbb_zbs_zicsr_zifencei_zicntr', mabi='lp64d',
                   mmu_mode='sv39', privilege_modes=['m', 's', 'u'])
    value.update(updates)
    return value


class LinuxTargetTest(unittest.TestCase):
    def test_boot_layout_is_capability_based_and_relocatable(self):
        value = linux_target(soc='another-core', boot={'payload_address': 0x90000000},
                             ram=[{'base': 0x90000000, 'size': 0x8000000}])
        layout = linux_layout(value)
        self.assertEqual(layout['firmware_address'], 0x90000000)
        self.assertEqual(layout['next_stage_address'], 0x90200000)
        self.assertEqual(layout['fdt_address'], 0x97ff0000)

    def test_rejects_targets_without_linux_boot_facilities(self):
        for update in ({'mmu_mode': 'bare'}, {'privilege_modes': ['m', 's']},
                       {'mabi': 'lp64'}, {'harts': [0, 1]},
                       {'ram': [{'base': 0x80000000, 'size': 0x4000000}]}):
            with self.subTest(update=update), self.assertRaises(ValueError):
                linux_layout(linux_target(**update))

    def test_preserves_additional_compiler_extensions(self):
        config = buildroot_architecture(linux_target(
            march='rv64imafdcbvh_zba_zbb_zbs_zicsr_zifencei_zicntr_zvfh_zvl128b'))
        self.assertIn('BR2_RISCV_ISA_RVV=y', config)
        self.assertIn('BR2_RISCV_ISA_EXTRA="b_h_zba_zbb_zbs_zicntr_zvfh_zvl128b"', config)


if __name__ == '__main__':
    unittest.main()
