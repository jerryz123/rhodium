# Checks shared architectural projection and exact-product co-simulation environments.
# SPDX-License-Identifier: Apache-2.0
import copy
import json
from pathlib import Path
import runpy
import unittest
from unittest.mock import patch

from test_arch_test import sail_default, vector_udb

SIMS = Path(__file__).resolve().parents[1]
COSIM = runpy.run_path(str(SIMS / "cosim/configure.py"))
ACT = runpy.run_path(str(SIMS / "arch-test/configure.py"))
from sail.configuration import project_architecture, reference_model_differences
from sail.product import fingerprint


def environment():
    def region(address, size, *, device=False, writable=True, cacheable=False):
        return {"range": {"address": address, "size": size}, "attributes": {
            "readable": True, "writable": writable, "executable": not device,
            "cacheable": cacheable, "atomic": cacheable, "device": device,
            "read_idempotent": not device, "cache_block_zero": cacheable,
            "instruction_cacheable": not device,
        }}
    return {
        "schema": 1, "reset_pc": 0x1000, "hart_id": 0,
        "clock_frequency_hz": 100000000, "timebase_frequency_hz": 1000000,
        "private_memory": [{"address": 0x1000, "size": 0x1000}, {"address": 0x80000000, "size": 0x10000}],
        "regions": [region(0x80000000, 0x10000, cacheable=True),
                    region(0x1000, 0x1000, writable=False), region(0x2000000, 0x10000, device=True)],
    }


class SailConfigurationTest(unittest.TestCase):
    def test_runtime_image_and_descriptor_are_embedded_exactly(self):
        env = environment()
        env.update(xlen=64, rom_image={"address": 0x1000, "bytes": [0, 255, 19, 0]})
        observations = {"schema": 1, "epoch": "host-reset", "harts": [{"instance": 0, "path": ["soc", "hart", "core"]}]}
        manifest = COSIM["runtime_manifest"]({"environment_fingerprint": fingerprint(env)}, env, observations)
        self.assertEqual(manifest["rom_image"], env["rom_image"])
        self.assertEqual(manifest["observations"], observations)
        header = COSIM["runtime_header"]({"quoted": 'a"b\\c'}, manifest)
        for line in header.splitlines():
            if line.startswith("inline constexpr char "):
                name, literal = line[len("inline constexpr char "):].split("[] = ", 1)
                value = json.loads(json.loads(literal[:-1]))
                self.assertEqual(value, manifest if name == "manifest" else {"quoted": 'a"b\\c'})
        for mutate in (lambda e, o: o["harts"].append(o["harts"][0]),
                       lambda e, o: o["harts"][0].update(instance=1),
                       lambda e, o: e["rom_image"].update(address=0x2000),
                       lambda e, o: e["rom_image"].update(bytes=[256]),
                       lambda e, o: e["rom_image"].update(bytes=[0] * 4097)):
            changed_env, changed_observations = copy.deepcopy(env), copy.deepcopy(observations)
            mutate(changed_env, changed_observations)
            with self.assertRaises(ValueError):
                COSIM["runtime_manifest"]({}, changed_env, changed_observations)

    def default(self):
        value = sail_default()
        value["platform"].update(clint={"supported": True}, simple_interrupt_generator={"supported": True})
        return value

    def test_architecture_does_not_mutate_defaults_or_environment(self):
        default = self.default()
        saved = copy.deepcopy(default)
        common = project_architecture(default, vector_udb())
        self.assertEqual(default, saved)
        self.assertEqual(common["memory"]["regions"], saved["memory"]["regions"])
        self.assertEqual(common["platform"]["clint"], saved["platform"]["clint"])
        self.assertEqual(common["extensions"]["V"]["vlen_exp"], 7)

    def test_act_and_cosim_use_identical_hart_projection(self):
        udb = vector_udb()
        act = ACT["sail_config"](self.default(), udb, 0x80000000, 0x40000000)
        cosim = COSIM["project_environment"](project_architecture(self.default(), udb), environment())
        for key in ("base", "extensions"):
            self.assertEqual(act[key], cosim[key])
        for key in act["memory"].keys() - {"regions", "dtb_address"}:
            self.assertEqual(act["memory"][key], cosim["memory"][key])
        self.assertTrue(act["platform"]["clint"]["supported"])
        self.assertFalse(cosim["platform"]["clint"]["supported"])
        self.assertFalse(cosim["platform"]["simple_interrupt_generator"]["supported"])
        self.assertEqual(cosim["platform"]["clock_frequency"], 100000000)

    def test_actual_map_backing_and_pmas_replace_all_default_regions(self):
        common = project_architecture(self.default(), vector_udb())
        saved = copy.deepcopy(common)
        result = COSIM["project_environment"](common, environment())
        self.assertEqual(common, saved)
        regions = result["memory"]["regions"]
        self.assertEqual([int(r["base"]["value"], 0) for r in regions], [0x1000, 0x2000000, 0x80000000])
        rom, device, ram = [r["attributes"] for r in regions]
        self.assertFalse(rom["writable"])
        self.assertTrue(rom["executable"])
        self.assertEqual(rom["mem_type"], "IOMemory")
        self.assertFalse(device["read_idempotent"])
        self.assertTrue(device["supports_pte_read"])
        self.assertEqual(device["misaligned_exceptions"]["load_store"], {"Some": "AccessFault"})
        self.assertEqual(ram["atomic_support"], "AMOArithmetic")
        self.assertEqual(ram["reservability"], "RsrvEventual")
        self.assertTrue(ram["supports_pte_read"])
        self.assertEqual(ram["misaligned_atomicity_granule_size_exp"], 0)

    def test_rejects_bad_maps_and_backing(self):
        common = project_architecture(self.default(), vector_udb())
        mutations = [
            lambda e: e["regions"].append(e["regions"][0]),
            lambda e: e["regions"][0]["range"].update(size=1),
            lambda e: e["regions"][0]["range"].update(address=1 << 44),
            lambda e: e.update(reset_pc=0x2000000),
            lambda e: e["private_memory"].append(e["regions"][2]["range"]),
            lambda e: e["private_memory"].append(e["private_memory"][0]),
        ]
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                env = environment()
                mutate(env)
                with self.assertRaises(ValueError):
                    COSIM["project_environment"](common, env)

    def test_subpage_uart_keeps_exact_extent(self):
        env = environment()
        env["regions"][2]["range"]["size"] = 8
        projected = COSIM["project_environment"](project_architecture(self.default(), vector_udb()), env)
        self.assertEqual(projected["memory"]["regions"][1]["size"]["value"], "0x8")
        env["regions"][2]["attributes"]["executable"] = True
        with self.assertRaises(ValueError):
            COSIM["project_environment"](project_architecture(self.default(), vector_udb()), env)

    def test_product_and_environment_identity_are_checked(self):
        config = {"product": "mini-rv5stage-rv32int"}
        env = environment()
        export = {"configuration": config, "environment": env,
                  "configuration_fingerprint": fingerprint(config), "environment_fingerprint": fingerprint(env)}
        check = COSIM["checked_product"]
        with patch.dict(check.__globals__, product_architecture=lambda c, n: {"name": n}):
            self.assertEqual(check(export, config["product"])[1], env)
            for field in ("configuration", "environment"):
                changed = copy.deepcopy(export)
                changed[field]["tampered"] = True
                with self.assertRaises(ValueError):
                    check(changed, config["product"])

    def test_reference_differences_remain_explicit(self):
        params = vector_udb()["params"]
        params["HPM_COUNTER_EN"] = [True] * 32
        differences = reference_model_differences(params)
        self.assertIn("HPM_EVENTS", differences)
        self.assertIn("RESERVED_VSET_X0X0_VILL_SET", differences)
        self.assertEqual(fingerprint({"b": 2, "a": 1}), fingerprint({"a": 1, "b": 2}))


if __name__ == "__main__":
    unittest.main()
