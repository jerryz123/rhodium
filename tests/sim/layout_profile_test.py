#!/usr/bin/env python3
# Checks IBS sample decoding and allocation attribution without PMU permissions.
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location(
    "layout_profile", Path(__file__).resolve().parents[2] / "sims/native/profile-layout.py")
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


def sample(address, level, operation=2, mode=2):
    return struct.pack("IHHQIIQQQ", 9, mode, 48, 0x4000, 123, 123,
                       address, 17, operation | (level << 33))


class LayoutProfileTest(unittest.TestCase):
    def test_precise_loads_and_lost_records(self):
        data = (sample(0x1010, 1) + sample(0x2008, 2) + sample(0x3010, 15)
                + sample(0x4000, 3, operation=4) + sample(0x5000, 2, mode=1)
                + sample(0, 2) + struct.pack("IHHQQ", 2, 0, 24, 99, 7))
        rows, lost = profile.decode_records(data)
        self.assertEqual(lost, 7)
        self.assertEqual([r["address"] for r in rows], [0x1010, 0x2008, 0x3010])
        self.assertEqual([r["level"] for r in rows], [1, 2, 15])

    def test_storage_ranges_take_priority_over_heap_mapping(self):
        layout = [dict(name="arena", start=0x1000, end=0x1100),
                  dict(name="ff_bank_a", start=0x2000, end=0x2040)]
        maps = [dict(start=0x1000, end=0x4000, file_offset=0,
                     permissions="rw-p", path="[heap]"),
                dict(start=0x8000, end=0x9000, file_offset=0x1000,
                     permissions="r--p", path="/model.so")]
        data = sample(0x1010, 1) + sample(0x2008, 2) + sample(0x3010, 15) + sample(0x8040, 3)
        rows, _ = profile.decode_records(data)
        result = profile.summarize(rows, layout, maps, Path("/model.so"))
        self.assertEqual(result["arena"]["L1"], 1)
        self.assertEqual(result["ff_bank_a"]["beyond_L1"], 1)
        self.assertEqual(result["[heap]"]["unknown_level"], 1)
        self.assertEqual(result["[heap]"]["beyond_L1"], 0)
        self.assertEqual(result["generated_readonly"]["L3"], 1)
        self.assertEqual(rows[-1]["offset"], 0x1040)
        self.assertEqual(profile.locate(0x1100, layout, maps, Path("/model.so"))[0], "[heap]")

    def test_invalid_records(self):
        for data in [b"\0", struct.pack("IHH", 9, 2, 0),
                     sample(0x1000, 1)[:-1], struct.pack("IHH", 9, 2, 8),
                     struct.pack("IHH", 2, 0, 8)]:
            with self.subTest(data=data), self.assertRaises(ValueError):
                profile.decode_records(data)


if __name__ == "__main__":
    unittest.main()
