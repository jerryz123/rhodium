# Checks shared architectural projection and exact-config co-simulation environments.
# SPDX-License-Identifier: Apache-2.0
import copy
import json
from pathlib import Path
import runpy
import unittest
from unittest.mock import Mock, patch

from test_arch_test import sail_default, vector_udb

SIMS = Path(__file__).resolve().parents[1]
COSIM = runpy.run_path(str(SIMS / "cosim/runtime/configure.py"))
ACT = runpy.run_path(str(SIMS / "arch-test/configure.py"))
from sail.configuration import project_architecture, reference_model_differences
from sail.soc_config import fingerprint, model_defaults
from sail.configuration import TRANSFORMED_INSTRUCTION_PARAMETERS


def environment():
    def region(address, size, *, device=False, writable=True, cacheable=False):
        return {"range": {"address": address, "size": size}, "attributes": {
            "readable": True, "writable": writable, "executable": not device,
            "cacheable": cacheable, "atomic": cacheable, "device": device,
            "read_idempotent": not device, "cache_block_zero": cacheable,
            "instruction_cacheable": not device,
        }}
    return {
        "reset_pc": 0x1000, "hart_id": 0,
        "clock_frequency_hz": 100000000, "timebase_frequency_hz": 1000000,
        "private_memory": [{"address": 0x1000, "size": 0x1000}, {"address": 0x80000000, "size": 0x10000}],
        "regions": [region(0x80000000, 0x10000, cacheable=True),
                    region(0x1000, 0x1000, writable=False), region(0x2000000, 0x10000, device=True)],
    }


class SailConfigurationTest(unittest.TestCase):
    def test_export_requires_matching_fingerprints_and_an_environment_object(self):
        configuration = {"config": "mini-rv5stage-rva23"}
        env = environment()
        exported = {"configuration": configuration, "environment": env,
                    "configuration_fingerprint": fingerprint(configuration),
                    "environment_fingerprint": fingerprint(env)}
        checked = COSIM["checked_config"]
        architecture = vector_udb()
        with patch.dict(checked.__globals__, config_architecture=Mock(return_value=architecture)):
            self.assertEqual(checked(exported, configuration["config"]), (architecture, env))
            for key in ("configuration", "environment"):
                changed = copy.deepcopy(exported)
                changed[key]["changed"] = True
                with self.assertRaises(ValueError):
                    checked(changed, configuration["config"])
            changed = {**exported, "environment": [], "environment_fingerprint": fingerprint([])}
            with self.assertRaises(ValueError):
                checked(changed, configuration["config"])

    def test_model_defaults_check_release_and_embedding_schema(self):
        default = self.default()
        default["extensions"]["H"]["transformed_instruction"] = dict.fromkeys(TRANSFORMED_INSTRUCTION_PARAMETERS, True)
        with patch.dict("sys.modules", {"pyjson5": Mock(decode=json.loads)}):
            for xlen in (32, 64):
                with patch("sail.soc_config.subprocess.check_output", side_effect=["0.16", json.dumps(default)]) as invoke:
                    self.assertEqual(model_defaults("sail", xlen), default)
                    args = invoke.call_args.args[0]
                    self.assertEqual("--rv32" in args, xlen == 32)
            for key in ("mcountinhibit", "transformed_instruction", "Sdtrig", "hedeleg", "hpm_events", "pmm_unsupported_write_to_disabled", "independent_context_csrs"):
                old = copy.deepcopy(default)
                target = old["extensions"]["H"] if key == "transformed_instruction" else old["extensions"] if key == "Sdtrig" else old["base"]
                del target[key]
                with patch("sail.soc_config.subprocess.check_output", side_effect=["0.16", json.dumps(old)]):
                    with self.assertRaisesRegex(ValueError, "rebuild with arch-test-sail-setup"):
                        model_defaults("old-sail", 64)
            with patch("sail.soc_config.subprocess.check_output", return_value="0.14.2"):
                with self.assertRaisesRegex(ValueError, "expected Sail 0.16"):
                    model_defaults("other-sail", 64)

    def test_no_trigger_harts_omit_tselect_in_both_projections(self):
        for xlen in (32, 64):
            udb = vector_udb()
            udb["params"]["MXLEN"] = xlen
            self.assertFalse(project_architecture(self.default(), udb)["extensions"]["Sdtrig"]["supported"])

    def test_misa_mutability_is_per_extension_and_stateen_matches_features(self):
        udb = vector_udb()
        udb["params"].update(MUTABLE_MISA_F=True, MUTABLE_MISA_D=False)
        default = self.default()
        default["platform"]["event_selectors"] = [{"event": "EV_FENCE", "selector": {"len": 64, "value": "0x1"}}]
        model = project_architecture(default, udb)
        self.assertEqual(model["platform"]["event_selectors"], [])
        self.assertTrue(model["extensions"]["F"]["mutable_misa"])
        self.assertFalse(model["extensions"]["D"]["mutable_misa"])
        stateen = model["extensions"]["Stateen"]
        self.assertEqual(stateen["mstateen0"]["CONTEXT"], "CSRBit_ReadOnlyZero")
        self.assertEqual(stateen["mstateen0"]["P1P13"], "CSRBit_ReadOnlyZero")
        self.assertEqual(stateen["mstateen0"]["SRMCFG"], "CSRBit_ReadOnlyZero")
        self.assertEqual(stateen["mstateen0"]["ENVCFG"], "CSRBit_ReadOnlyZero")

    def test_fixed_reservation_regions_follow_the_udb_strategy(self):
        for size in (64, 128):
            udb = vector_udb()
            udb["implemented_extensions"].append({"name": f"Za{size}rs", "version": "1.0.0"})
            udb["params"]["LRSC_RESERVATION_STRATEGY"] = f"reserve naturally-aligned {size}-byte region"
            udb["params"]["LRSC_FAIL_ON_NON_EXACT_LRSC"] = False
            reservation = project_architecture(self.default(), udb)["platform"]["reservation"]
            self.assertEqual(reservation["reservation_set_size_exp"], size.bit_length() - 1)
            self.assertFalse(reservation["require_exact_reservation_addr"])

    def test_explicit_csr_warl_is_shared_by_act_and_cosim(self):
        udb = vector_udb()
        policy = {"medeleg_mask": 0xcb3fe, "hedeleg_mask": 0, "pmm_unsupported_write": "disabled", "stateen_context": False}
        common = project_architecture(self.default(), udb, policy)
        act = ACT["sail_config"](self.default(), udb, 0x80000000, 0x40000000, policy)
        cosim = COSIM["project_environment"](common, environment())
        self.assertEqual(act["base"], cosim["base"])
        self.assertEqual(common["base"]["medeleg"]["delegatable_bits"]["value"], "0xcb3fe")
        self.assertTrue(common["base"]["pmm_unsupported_write_to_disabled"])
        policy["medeleg_mask"] = 0x8b3fe
        policy["pmm_unsupported_write"] = "preserve"
        other = project_architecture(self.default(), udb, policy)
        self.assertEqual(other["base"]["medeleg"]["delegatable_bits"]["value"], "0x8b3fe")
        self.assertFalse(other["base"]["pmm_unsupported_write_to_disabled"])

    def test_context_state_enables_follow_static_implementation_policy(self):
        udb = vector_udb()
        udb["implemented_extensions"] += [{"name": "S", "version": "1.13.0"}, {"name": "Smstateen", "version": "1.0.0"}]
        udb["params"]["MSTATEEN_ENVCFG_TYPE"] = "rw"
        default = self.default()
        default["extensions"]["S"] = {"supported": False}
        for context in (False, True):
            policy = {"medeleg_mask": 0xcb3fe, "hedeleg_mask": 0, "pmm_unsupported_write": "disabled", "stateen_context": context}
            act = ACT["sail_config"](default, udb, 0x80000000, 0x40000000, policy)
            cosim = project_architecture(default, udb, policy)
            for model in (act, cosim):
                self.assertFalse(model["extensions"]["Sdtrig"]["supported"])
                expected = "CSRBit_Writable" if context else "CSRBit_ReadOnlyZero"
                self.assertEqual(model["extensions"]["Stateen"]["mstateen0"]["CONTEXT"], expected)
                self.assertEqual(model["extensions"]["Stateen"]["hstateen0"]["CONTEXT"], expected)
                self.assertEqual(model["base"]["independent_context_csrs"], context)
                self.assertEqual(model["base"]["scontext_writable_mask"]["value"], "0xffffffff")
                self.assertEqual(model["base"]["mcontext_writable_mask"]["value"], "0x1fff")

    def test_csr_warl_rejects_invalid_delegation(self):
        udb = vector_udb()
        policy = {"medeleg_mask": 0xcb3fe, "hedeleg_mask": 0, "pmm_unsupported_write": "disabled", "stateen_context": False}
        for name, value in (("medeleg_mask", 1 << 11), ("medeleg_mask", 1 << 16),
                            ("medeleg_mask", -1), ("medeleg_mask", True),
                            ("hedeleg_mask", 1), ("pmm_unsupported_write", "unknown"), ("stateen_context", 1), ("stateen_context", True)):
            with self.assertRaises(ValueError):
                project_architecture(self.default(), udb, {**policy, name: value})
        udb["implemented_extensions"] += [{"name": "H", "version": "1.0.0"}, {"name": "C", "version": "2.0"}]
        # Test the policy directly, independently of other H projection requirements.
        from sail.configuration import project_csr_warl
        policy.update(medeleg_mask=0xfcb7fe, hedeleg_mask=0xcb1fe)
        project_csr_warl(self.default()["base"], policy, {"H": "1.0.0", "C": "2.0"}, 64)
        for mask in (0x8b1fe, 0x4b1fe, 0xcb3fe, 0, 0xcb1fe & ~(1 << 1), 0xcb1fe & ~(1 << 4), 0xcb1fe & ~(1 << 6)):
            with self.assertRaises(ValueError):
                project_csr_warl(self.default()["base"], {**policy, "hedeleg_mask": mask}, {"H": "1.0.0", "C": "2.0"}, 64)
        with self.assertRaises(ValueError):
            project_csr_warl(self.default()["base"], policy, {"H": "1.0.0"}, 64)

    def test_hpm_event_choices_come_from_udb(self):
        for events in ([0, 1, 2], [0, 9]):
            udb = vector_udb()
            udb["params"]["HPM_COUNTER_EN"][3] = True
            udb["params"]["HPM_EVENTS"] = events
            model = project_architecture(self.default(), udb)
            self.assertEqual(model["base"]["hpm_events"], {"restricted": True, "supported": [
                {"len": 32, "value": hex(event)} for event in events]})
        for events in (None, [], [1], [0, -1], [0, True], [0, 1 << 32]):
            udb["params"]["HPM_EVENTS"] = events
            with self.assertRaisesRegex(ValueError, "HPM_EVENTS"):
                project_architecture(self.default(), udb)

    def test_mcountinhibit_projects_presence_and_exact_writable_mask(self):
        for supported, enabled in ((False, ()), (True, ()), (True, (0, 2)), (True, (3, 31))):
            udb = vector_udb()
            udb["params"].update(MCOUNTINHIBIT_IMPLEMENTED=supported,
                                 COUNTINHIBIT_EN=[i in enabled for i in range(32)])
            result = project_architecture(self.default(), udb)
            self.assertEqual(result["base"]["mcountinhibit"], {
                "supported": supported,
                "writable_bits": {"len": 32, "value": hex(sum(1 << i for i in enabled))},
            })
            self.assertNotIn("COUNTINHIBIT_EN", reference_model_differences(udb["params"]))
        for supported, mask in ((True, [False, True] + [False] * 30),
                                (False, [True] + [False] * 31),
                                (True, [False] * 31), (True, [0] * 32)):
            udb = vector_udb()
            udb["params"].update(MCOUNTINHIBIT_IMPLEMENTED=supported, COUNTINHIBIT_EN=mask)
            with self.assertRaisesRegex(ValueError, "COUNTINHIBIT_EN"):
                project_architecture(self.default(), udb)
        udb = vector_udb()
        udb["params"]["MCOUNTINHIBIT_IMPLEMENTED"] = True
        with self.assertRaisesRegex(ValueError, "COUNTINHIBIT_EN"):
            project_architecture(self.default(), udb)

    def test_new_optional_extensions_do_not_leak_from_model_defaults(self):
        default = self.default()
        for name in ("Ssstrict", "Smdbltrp", "Ssdbltrp"):
            default["extensions"][name] = {"supported": True}
        result = project_architecture(default, vector_udb())
        for name in ("Ssstrict", "Smdbltrp", "Ssdbltrp"):
            self.assertFalse(result["extensions"][name]["supported"])

    def test_runtime_image_and_descriptor_are_embedded_exactly(self):
        env = environment()
        env.update(xlen=64, rom_image={"address": 0x1000, "bytes": [0, 255, 19, 0]})
        observations = {"epoch": "host-reset", "harts": [{"instance": 0, "path": ["soc", "hart", "core"]}]}
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
                       lambda e, o: o.update(epoch="unknown"),
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
        self.assertEqual(cosim["extensions"]["F"]["fflags_dirty_policy"], "Fflags_Dirty_Instruction")

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

    def test_config_and_environment_identity_are_checked(self):
        config = {"config": "mini-rv5stage-rv32int"}
        env = environment()
        export = {"configuration": config, "environment": env,
                  "configuration_fingerprint": fingerprint(config), "environment_fingerprint": fingerprint(env)}
        check = COSIM["checked_config"]
        with patch.dict(check.__globals__, config_architecture=lambda c, n: {"name": n}):
            self.assertEqual(check(export, config["config"])[1], env)
            for field in ("configuration", "environment"):
                changed = copy.deepcopy(export)
                changed[field]["tampered"] = True
                with self.assertRaises(ValueError):
                    check(changed, config["config"])

    def test_reference_differences_remain_explicit(self):
        params = vector_udb()["params"]
        params["HPM_COUNTER_EN"] = [True] * 32
        differences = reference_model_differences(params)
        self.assertIn("HPM_EVENTS", differences)
        self.assertIn("RESERVED_VSET_X0X0_VILL_SET", differences)
        self.assertEqual(fingerprint({"b": 2, "a": 1}), fingerprint({"a": 1, "b": 2}))


if __name__ == "__main__":
    unittest.main()
