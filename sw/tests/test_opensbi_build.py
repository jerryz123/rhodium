#!/usr/bin/env python3
# Checks capability-based OpenSBI layout selection and RAM reservations.
# SPDX-License-Identifier: Apache-2.0
import importlib.util
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


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

    def test_shared_bundle_relocates_firmware_checks_payload_and_binds_without_compiler(self):
        from bind import bind
        from program_target import target_fingerprint
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_target = target()
            layout = self.adapter.target_layout(source_target)

            def elf(name, kind, address):
                header = b'\x7fELF\x02\x01\x01' + bytes(9)
                header += struct.pack('<HHIQQQIHHHHHH', kind, 243, 1, address, 64, 0, 0, 64, 56, 1, 0, 0, 0)
                segment = struct.pack('<IIQQQQQQ', 1, 5, 120, address, address, 1, 128, 1)
                (root / name).write_bytes(header + segment + b'\x00')

            elf('fw_jump.elf', 3, 0)
            elf('opensbi_smoke.elf', 2, layout['next_stage_address'])
            fdt = root / 'platform.dtb'
            fdt.write_bytes(struct.pack('>II', 0xd00dfeed, 8))
            execution_fdt = root / 'execution.dtb'
            execution_fdt.write_bytes(fdt.read_bytes())
            (root / 'build-configuration.json').write_text(json.dumps(dict(
                fdt_sha256=hashlib.sha256(fdt.read_bytes()).hexdigest(),
                layout={key: value for key, value in layout.items() if key != 'soc'})))
            source = root / 'target.json'
            source.write_text(json.dumps(source_target))
            with patch.object(self.adapter, 'readelf_for', return_value='readelf'), \
                    patch.object(self.adapter.subprocess, 'check_output', return_value='no HTIF symbols'):
                self.adapter.write_manifest(SimpleNamespace(target=source, output=root, compiler='gcc'))
            manifest_path, run_path = root / 'manifest.json', root / 'run-manifest.json'
            original = manifest_path.read_bytes()
            destination = source_target | dict(soc='another-core', resolved_configuration={'core': 'other'})
            with patch('subprocess.check_output', side_effect=AssertionError('binding invoked a toolchain')):
                bind(manifest_path, destination, run_path, execution_fdt)
            self.assertEqual(manifest_path.read_bytes(), original)
            result = json.loads(run_path.read_text())
            self.assertEqual(result['target_fingerprint'], target_fingerprint(destination))
            self.assertEqual(result['build_target'], source_target)
            with self.assertRaisesRegex(ValueError, 'requires the execution target device tree'):
                bind(manifest_path, destination, run_path)
            changed_fdt = root / 'other.dtb'
            changed_fdt.write_bytes(fdt.read_bytes() + b'other platform')
            with self.assertRaisesRegex(ValueError, 'device tree does not match'):
                bind(manifest_path, destination, run_path, changed_fdt)
            for image in ('fw_jump.elf', 'opensbi_smoke.elf', 'platform.dtb'):
                saved = (root / image).read_bytes()
                (root / image).write_bytes(saved + b'changed')
                with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                    bind(manifest_path, destination, run_path, execution_fdt)
                (root / image).write_bytes(saved)
            manifest = json.loads(manifest_path.read_text())
            firmware = root / 'fw_jump.elf'
            data = firmware.read_bytes()
            firmware.write_bytes(data[:104] + struct.pack('<Q', 0x200001) + data[112:])
            manifest['tests'][0]['sha256'] = hashlib.sha256(firmware.read_bytes()).hexdigest()
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'overlaps its next stage'):
                bind(manifest_path, destination, run_path, execution_fdt)

    def test_qualification_run_targets_do_not_build_or_elaborate(self):
        repo = SCRIPT.parents[2]
        for name, soc, timeout, cycles in (('opensbi-smoke-run', 'simple-spike-rva23', '1800', '50000000'),
                                          ('litmus-smoke-run', 'tiled-spike-rva23', '600', '2000000')):
            result = subprocess.run(['make', '-n', '-C', str(repo / 'sims'), name, 'SOC=' + soc,
                                     'PREBUILT_SIMULATOR=/tmp/prebuilt/VTestDriver',
                                     'PROGRAM_MANIFEST=/tmp/shared/run-manifest.json'],
                                    check=True, capture_output=True, text=True)
            self.assertIn('program-test/run.py --manifest "/tmp/shared/run-manifest.json"', result.stdout)
            self.assertIn('--timeout ' + timeout, result.stdout)
            self.assertIn('--max-cycles ' + cycles, result.stdout)
            self.assertNotIn('sw/build/', result.stdout)
            self.assertNotIn('tools/run-racket', result.stdout)


if __name__ == '__main__':
    unittest.main()
