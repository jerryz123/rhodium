# Exercises generic SRAM mapping, scoped site selection, and generated-wrapper behavior.
# SPDX-License-Identifier: Apache-2.0

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


TEST_DIR = Path(__file__).resolve().parent
SRAM_DIR = TEST_DIR.parent
SKY130_DIR = SRAM_DIR / "sky130"
MAPPER = SRAM_DIR / "map-memories.py"
CATALOG = SKY130_DIR / "macros.ini"
FUNCTIONAL_MODEL = (
    SKY130_DIR
    / "models"
    / "sky130_sram_2kbyte_1rw1r_32x512_8.functional.sv"
)
FIXTURES = TEST_DIR / "fixtures"
CIRCT_OPT = Path(os.environ.get("CIRCT_OPT", "circt-opt"))
MEMORY_SITE_PLUGIN = Path(os.environ.get("MEMORY_SITE_PLUGIN", "rhodium-memory-sites.so"))


class MapperTest(unittest.TestCase):
    def invoke_mapper(
        self,
        fixture,
        output_dir: Path,
        expect_success: bool = True,
        site_inventory: Path = None,
    ):
        verilog = output_dir / "wrappers.sv"
        manifest = output_dir / "manifest.json"
        input_mlir = fixture if isinstance(fixture, Path) else FIXTURES / fixture
        command = [
            os.environ.get("PYTHON", "python3"),
            str(MAPPER),
            str(input_mlir),
            "--catalog",
            str(CATALOG),
            "--output-verilog",
            str(verilog),
            "--output-manifest",
            str(manifest),
        ]
        if site_inventory is not None:
            command.extend(["--site-inventory", str(site_inventory)])
        result = subprocess.run(command, text=True, capture_output=True)
        if expect_success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result, verilog, manifest

    def invoke_site_pass(
        self,
        policy: Path,
        output_dir: Path,
        expect_success: bool = True,
        actual_top: str = "Top",
        scope_prefix: str = "leaf",
    ):
        output_dir.mkdir(parents=True, exist_ok=True)
        selected_mlir = output_dir / "selected.mlir"
        inventory = output_dir / "site-inventory.json"
        mapping_options = f"policy={policy} inventory={inventory} top={actual_top}"
        if scope_prefix:
            mapping_options += f" scope-prefix={scope_prefix}"
        pipeline = (
            "builtin.module("
            f"rhodium-select-hw-top{{top={actual_top}}},"
            "hw-flatten-modules{hw-inline-all=true hw-inline-with-state=true},"
            "symbol-dce,"
            f"rhodium-map-memory-sites{{{mapping_options}}},"
            "symbol-dce)"
        )
        result = subprocess.run(
            [
                str(CIRCT_OPT),
                f"--load-pass-plugin={MEMORY_SITE_PLUGIN}",
                f"--pass-pipeline={pipeline}",
                str(FIXTURES / "site-selective.mlir"),
                "-o",
                str(selected_mlir),
            ],
            text=True,
            capture_output=True,
        )
        if expect_success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result, selected_mlir, inventory

    def test_manifest_and_wrapper_shape(self):
        with tempfile.TemporaryDirectory(prefix="rhodium-memory-map-") as temporary:
            result, verilog, manifest_path = self.invoke_mapper(
                "banked-width-masked.mlir", Path(temporary)
            )
            self.assertIn(
                "mapped 1 FIRRTLMem definitions to 4 macro instances", result.stdout
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            mapping = manifest["memories"][0]["mapping"]
            self.assertEqual(mapping["interface"], "openram_1rw1r")
            self.assertEqual(mapping["depth_banks"], 2)
            self.assertEqual(mapping["width_slices"], 2)
            self.assertEqual(mapping["instances_per_definition"], 4)
            self.assertEqual(mapping["logical_bits"], 40960)
            self.assertEqual(mapping["allocated_bits"], 65536)
            self.assertEqual(
                manifest["memories"][0]["functional_verilog"],
                str(FUNCTIONAL_MODEL.resolve()),
            )
            wrapper = verilog.read_text(encoding="utf-8")
            self.assertIn("module storage_1024x40(", wrapper)
            self.assertIn("current_bank = RW0_addr[9:9]", wrapper)
            self.assertIn("macro_dout_d1_w1[7:0]", wrapper)
            self.assertIn("RW0_wmask[4]", wrapper)

    def test_unsupported_port_topology_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="rhodium-memory-map-") as temporary:
            result, _, _ = self.invoke_mapper(
                "unsupported-two-rw.mlir", Path(temporary), expect_success=False
            )
            self.assertIn("exactly one read-write port is required", result.stderr)

    def test_generated_wrapper_behavior(self):
        verilator_setting = os.environ.get("VERILATOR", "verilator")
        verilator = (
            shutil.which(verilator_setting)
            if "/" not in verilator_setting
            else verilator_setting
        )
        self.assertTrue(
            verilator and Path(verilator).is_file(),
            f"Verilator not found: {verilator_setting}",
        )
        with tempfile.TemporaryDirectory(prefix="rhodium-memory-map-") as temporary:
            output_dir = Path(temporary)
            _, wrapper, _ = self.invoke_mapper("banked-width-masked.mlir", output_dir)
            object_dir = output_dir / "obj"
            compile_result = subprocess.run(
                [
                    str(verilator),
                    "--binary",
                    "--timing",
                    "--top-module",
                    "mapper_tb",
                    "-Wno-fatal",
                    "-Wno-DECLFILENAME",
                    "--Mdir",
                    str(object_dir),
                    str(wrapper),
                    str(FUNCTIONAL_MODEL),
                    str(FIXTURES / "mapper-tb.sv"),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            simulation = subprocess.run(
                [str(object_dir / "Vmapper_tb")], text=True, capture_output=True
            )
            self.assertEqual(simulation.returncode, 0, simulation.stderr)
            self.assertIn("mapper functional test PASS", simulation.stdout)

    def test_site_selective_mapping_preserves_inferred_equal_shape(self):
        self.assertTrue(CIRCT_OPT.is_file(), f"circt-opt not found: {CIRCT_OPT}")
        self.assertTrue(
            MEMORY_SITE_PLUGIN.is_file(),
            f"memory-site plugin not found: {MEMORY_SITE_PLUGIN}",
        )
        with tempfile.TemporaryDirectory(prefix="rhodium-memory-sites-") as temporary:
            output_dir = Path(temporary)
            _, selected_mlir, inventory_path = self.invoke_site_pass(
                FIXTURES / "site-selective-policy.yaml", output_dir
            )
            inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
            self.assertEqual(inventory["top"], "Top")
            self.assertEqual(inventory["policy_top"], "Leaf")
            self.assertEqual(inventory["scope_prefix"], "leaf")
            self.assertEqual(
                [
                    (site["path"], site["instance_path"], site["decision"])
                    for site in inventory["sites"]
                ],
                [
                    ("left/storage", "leaf/left/storage", "macro"),
                    ("right/storage", "leaf/right/storage", "infer"),
                ],
            )
            selected = selected_mlir.read_text(encoding="utf-8")
            self.assertIn(
                'hw.instance "leaf/left/storage_ext" @rhodium_sram_left_storage_',
                selected,
            )
            self.assertIn(
                'hw.instance "leaf/right/storage_ext" @storage_64x52', selected
            )
            self.assertIn("hw.module.generated private @storage_64x52", selected)
            self.assertNotIn("hw.module @Leaf", selected)

            result, wrappers, manifest_path = self.invoke_mapper(
                selected_mlir, output_dir, site_inventory=inventory_path
            )
            self.assertIn("mapped 1 of 2 memory sites to 2 macro instances", result.stdout)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            totals = manifest["totals"]
            self.assertEqual(totals["memory_sites"], 2)
            self.assertEqual(totals["mapped_sites"], 1)
            self.assertEqual(totals["inferred_sites"], 1)
            self.assertEqual(totals["macro_instances"], 2)

            rtl_result = subprocess.run(
                [
                    str(CIRCT_OPT),
                    "--lower-seq-to-sv=disable-mem-randomization=true disable-reg-randomization=true",
                    "--hw-memory-sim=disable-mem-randomization=true disable-reg-randomization=true read-enable-mode=undefined",
                    "--symbol-dce",
                    "--export-verilog",
                    str(selected_mlir),
                    "-o",
                    "/dev/null",
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(rtl_result.returncode, 0, rtl_result.stderr)
            self.assertIn("module storage_64x52", rtl_result.stdout)
            self.assertIn("rhodium_sram_left_storage_", rtl_result.stdout)
            rtl = output_dir / "site-selective.sv"
            rtl.write_text(rtl_result.stdout, encoding="utf-8")

            verilator_setting = os.environ.get("VERILATOR", "verilator")
            verilator = (
                shutil.which(verilator_setting)
                if "/" not in verilator_setting
                else verilator_setting
            )
            self.assertTrue(
                verilator and Path(verilator).is_file(),
                f"Verilator not found: {verilator_setting}",
            )
            lint = subprocess.run(
                [
                    str(verilator),
                    "--lint-only",
                    "--timing",
                    "--top-module",
                    "Top",
                    "-Wno-fatal",
                    "-Wno-DECLFILENAME",
                    "-Wno-UNUSEDSIGNAL",
                    "-Wno-PINCONNECTEMPTY",
                    str(rtl),
                    str(wrappers),
                    str(FUNCTIONAL_MODEL),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(lint.returncode, 0, lint.stderr)

    def test_unknown_memory_site_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="rhodium-memory-sites-") as temporary:
            output_dir = Path(temporary)
            policy = output_dir / "unknown-site.yaml"
            policy.write_text(
                "# Exercises rejection of a misspelled memory site.\n\n"
                "schema_version: 1\n"
                "top: Leaf\n"
                "default: infer\n"
                "sites:\n"
                "  missing/storage: sky130_sram_2kbyte_1rw1r_32x512_8\n",
                encoding="utf-8",
            )
            result, _, _ = self.invoke_site_pass(
                policy, output_dir, expect_success=False
            )
            self.assertIn("unknown site: missing/storage", result.stderr)
            self.assertIn("available sites: left/storage, right/storage", result.stderr)

    def test_scope_prefix_preserves_policy_relative_wrapper_identity(self):
        with tempfile.TemporaryDirectory(prefix="rhodium-memory-scope-") as temporary:
            output_dir = Path(temporary)
            _, scoped_mlir, _ = self.invoke_site_pass(
                FIXTURES / "site-selective-policy.yaml", output_dir / "scoped"
            )
            _, direct_mlir, direct_inventory_path = self.invoke_site_pass(
                FIXTURES / "site-selective-policy.yaml",
                output_dir / "direct",
                actual_top="Leaf",
                scope_prefix="",
            )
            wrapper_pattern = re.compile(r"@(?P<name>rhodium_sram_left_storage_[0-9a-f]+)")
            scoped_wrapper = wrapper_pattern.search(
                scoped_mlir.read_text(encoding="utf-8")
            )
            direct_wrapper = wrapper_pattern.search(
                direct_mlir.read_text(encoding="utf-8")
            )
            self.assertIsNotNone(scoped_wrapper)
            self.assertIsNotNone(direct_wrapper)
            self.assertEqual(scoped_wrapper.group("name"), direct_wrapper.group("name"))
            direct_inventory = json.loads(
                direct_inventory_path.read_text(encoding="utf-8")
            )
            self.assertEqual(direct_inventory["top"], "Leaf")
            self.assertEqual(direct_inventory["scope_prefix"], "")


if __name__ == "__main__":
    unittest.main()
