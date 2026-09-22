# Tests shared materialization and content identity for patched RISC-V submodules.
# SPDX-License-Identifier: Apache-2.0
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest


RISCV_DIR = Path(__file__).resolve().parents[1]
TOOL = RISCV_DIR / "patched_submodule.py"
SPEC = importlib.util.spec_from_file_location("patched_submodule", TOOL)
patched_submodule = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(patched_submodule)


class PatchedSubmoduleTest(unittest.TestCase):
    def test_materializes_ordered_series_without_modifying_upstream(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "upstream"
            source.mkdir()
            (source / "value.txt").write_text("before\n")
            (source / ".git").mkdir()
            (source / ".git/metadata").write_text("not copied\n")
            patches = root / "patches"
            patches.mkdir()
            (patches / "series").write_text("# Fixture series.\n0001-change-value.patch\n")
            (patches / "0001-change-value.patch").write_text(
                "diff --git a/value.txt b/value.txt\n"
                "--- a/value.txt\n"
                "+++ b/value.txt\n"
                "@@ -1 +1 @@\n"
                "-before\n"
                "+after\n"
            )

            output = root / "build/source"
            patched_submodule.materialize(source, patches / "series", output)

            self.assertEqual((source / "value.txt").read_text(), "before\n")
            self.assertEqual((output / "value.txt").read_text(), "after\n")
            self.assertFalse((output / ".git").exists())

    def test_rejects_output_inside_pristine_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "upstream"
            source.mkdir()
            (source / "value.txt").write_text("before\n")
            patches = root / "patches"
            patches.mkdir()
            (patches / "series").write_text("0001.patch\n")
            (patches / "0001.patch").write_text("unused\n")

            with self.assertRaisesRegex(ValueError, "must not contain one another"):
                patched_submodule.materialize(source, patches / "series", source / "patched")

    def test_identity_covers_gitlink_order_and_patch_contents(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "--quiet", str(root)], check=True)
            patches = root / "patches"
            patches.mkdir()
            series = patches / "series"
            series.write_text("0001.patch\n0002.patch\n")
            first_patch = patches / "0001.patch"
            first_patch.write_text("first\n")
            (patches / "0002.patch").write_text("second\n")
            subprocess.run(
                ["git", "-C", str(root), "update-index", "--add", "--cacheinfo",
                 f"160000,{'1' * 40},vendor/upstream"],
                check=True,
            )

            before = patched_submodule.identity(root, "vendor/upstream", series)
            series.write_text("0002.patch\n0001.patch\n")
            reordered = patched_submodule.identity(root, "vendor/upstream", series)
            series.write_text("0001.patch\n0002.patch\n")
            first_patch.write_text("changed\n")
            patched = patched_submodule.identity(root, "vendor/upstream", series)
            first_patch.write_text("first\n")
            subprocess.run(
                ["git", "-C", str(root), "update-index", "--add", "--cacheinfo",
                 f"160000,{'2' * 40},vendor/upstream"],
                check=True,
            )
            repinned = patched_submodule.identity(root, "vendor/upstream", series)

            self.assertEqual(len({before, reordered, patched, repinned}), 4)


if __name__ == "__main__":
    unittest.main()
