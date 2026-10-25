# Checks software-target architecture and ELF load ranges against RAM.
# SPDX-License-Identifier: Apache-2.0
import importlib.util
import struct
from pathlib import Path
import tempfile
import unittest

BUILD_SCRIPTS = Path(__file__).resolve().parents[1] / 'build'


def program_target(soc='single-core-rv5stage-soc'):
    return dict(soc=soc, xlen=64, harts=[0], extensions=['i', 'm'], march='rv64im',
                mabi='lp64', clock_frequency_hz=100000000,
                ram=[dict(base=0x80000000, size=0x10000)])


class ProgramTargetTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('program_target', BUILD_SCRIPTS / 'program_target.py')
        self.target = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.target)

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


class ELFMemoryTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('program_build', BUILD_SCRIPTS / 'build.py')
        self.builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.builder)

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
