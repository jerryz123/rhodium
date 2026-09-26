# Checks UDB-to-Sail projection, privileged-inclusive generation, and ACT completion.
# SPDX-License-Identifier: Apache-2.0
import importlib.util
from itertools import product
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
from types import ModuleType
import unittest
from unittest.mock import Mock, patch

RUNNER = Path(__file__).resolve().parents[1] / "arch-test" / "run.py"
RVMODEL_MACROS = RUNNER.with_name("rvmodel_macros.h")
VECTOR_PARAMETERS = {
    "FOLLOW_VTYPE_RESET_RECOMMENDATION": True,
    "IMPRECISE_VECTOR_TRAP_SETTABLE": False,
    "LEGAL_VSTART": "1_stride",
    "RESERVED_VSET_X0X0_VILL_SET": "never",
    "RESERVED_VSET_X0X0_VLMAX_CHANGE": "never",
    "RVV_VL_WHEN_AVL_LT_DOUBLE_VLMAX": "VLMAX",
    "SUPPORT_FRACTIONAL_LMUL_BEYOND_REQUIRED": "no_unrequired_supported",
    "VECTOR_FF_NO_EXCEPTION_TRIM": False,
    "VECTOR_FF_SEG_EXCEPTION_PARTIAL_LOAD": "custom",
    "VECTOR_FF_UPDATE_PAST_TRIM": "update_none",
    "VECTOR_LOAD_PAST_TRAP": False,
    "VECTOR_LOAD_SEG_FF_OVERWRITE_ELEMENTS_AFTER_FAULT": "no_overwrite",
    "VECTOR_LS_INDEX_MAX_EEW": "64",
    "VECTOR_LS_MISALIGNED_LEGAL": False,
    "VECTOR_LS_SEG_PARTIAL_ACCESS": True,
    "VECTOR_LS_WHOLEREG_MISALIGNED_LEGAL": False,
    "VFREDUSUM_FINAL_NODE_ELEMENT_BEHAVIOR": "copy",
    "VFREDUSUM_INACTIVE_NODE_ELEMENT_BEHAVIOR": "copy",
    "VFREDUSUM_NAN": "no_change",
    "VFREDUSUM_NODE_ROUNDING_BEHAVIOR": "SEW_precision",
    "VSSTATUS_VS_EXISTS": False,
}


def sail_default():
    return {
        "extensions": {
            "F": {"supported": False}, "D": {"supported": False},
            "Svade": {"supported": False}, "Zihpm": {"supported": False},
            "Svbare": {"supported": False, "sfence_vma_illegal_if_svbare_only": False},
            "Zicfilp": {"supported": False}, "Zicfiss": {"supported": False},
            "V": {
                "support_level": "Disabled", "vlen_exp": 3, "elen_exp": 3,
                "reserved_behavior": {"illegal_vtype": "IllegalVtype_SetVill",
                                      "vstart_out_of_bounds": "Vstart_Illegal"},
                "vl_use_ceil": False, "max_index_eew_exp": 3,
                "vstart": {"zero_required": {"arith": True, "scalar_move": True}},
            },
            "Zvfh": {"supported": False}, "Zvbb": {"supported": False},
            "Zvkb": {"supported": False}, "Zvkt": {"supported": False},
            "Zic64b": {"supported": False}, "Zicboz": {"supported": False},
            "Zicbom": {"supported": False}, "Zicbop": {"supported": False},
            "Ssnpm": {"supported": False, "supported_pmlen_7": False,
                       "supported_pmlen_16": False},
            "Stateen": {"Smstateen": {"supported": False}, "Ssstateen": {"supported": False}},
        },
        "base": {
            "mtvec": {"direct": {}, "vectored": {}}, "stvec": {"direct": {}, "vectored": {}},
            "mstatus": {}, "xtval_nonzero": {},
            "medeleg": {"delegatable_bits": {"len": 64, "value": "0xfc_b7ff"}},
        },
        "memory": {"asidlen": 16, "pmp": {}, "misaligned": {"exceptions": {}}, "regions": [
            {"attributes": {"mem_type": "MainMemory", "cacheable": True, "supports_cbo_zero": False}},
            {"base": {"len": 64, "value": "0x2000000"},
             "size": {"len": 64, "value": "0x10000000"},
             "attributes": {"mem_type": "IO", "cacheable": False, "supports_cbo_zero": True}},
        ]},
        "platform": {"reservation": {"reservation_set_size_exp": 3}, "cache_block_size_exp": 9,
                     "archid": 0, "impid": 0, "vendorid": 0},
    }


def architecture_params(asid_width=0):
    params = dict(MXLEN=64, NUM_PMP_ENTRIES=0, MISALIGNED_LDST=False,
                  MISALIGNED_LDST_EXCEPTION_PRIORITY="high", M_MODE_ENDIANNESS="little",
                  HPM_COUNTER_EN=[False] * 32, MCOUNTENABLE_EN=[False] * 32,
                  SCOUNTENABLE_EN=[False] * 32, MTVEC_MODES=[0, 1], STVEC_MODES=[0, 1],
                  MTVEC_BASE_ALIGNMENT_DIRECT=4, MSTATUS_FS_LEGAL_VALUES=[0],
                  MSTATUS_VS_LEGAL_VALUES=[0], PHYS_ADDR_WIDTH=44, ASID_WIDTH=asid_width,
                  LRSC_FAIL_ON_NON_EXACT_LRSC=True)
    for parameter in ("REPORT_ENCODING_IN_MTVAL_ON_ILLEGAL_INSTRUCTION",
                      "REPORT_VA_IN_MTVAL_ON_BREAKPOINT", "REPORT_VA_IN_MTVAL_ON_LOAD_MISALIGNED",
                      "REPORT_VA_IN_MTVAL_ON_STORE_AMO_MISALIGNED",
                      "REPORT_VA_IN_MTVAL_ON_INSTRUCTION_MISALIGNED"):
        params[parameter] = True
    return params


def vector_udb():
    params = architecture_params()
    params.update(VLEN=128, ELEN=64, SEW_MIN=8, VILL_SET_ON_RESERVED_VTYPE=True,
                  HW_MSTATUS_VS_DIRTY_UPDATE="precise", MSTATUS_FS_LEGAL_VALUES=[0, 1, 2, 3],
                  MSTATUS_VS_LEGAL_VALUES=[0, 1, 2, 3])
    params.update(VECTOR_PARAMETERS)
    names = ["F", "D", "V", "Zve32x", "Zve32f", "Zve64x", "Zve64f", "Zve64d",
             "Zvfh", "Zvkb", "Zvbb", "Zvkt", "Zvl32b", "Zvl64b", "Zvl128b"]
    extensions = [{"name": "Sm", "version": "= 1.12.0"}]
    extensions += [{"name": name, "version": "= 1.0.0"} for name in names]
    return {"params": params, "implemented_extensions": extensions}


class ArchTestConfigTest(unittest.TestCase):
    def test_generated_platform_identity_and_memory(self):
        settings = runpy.run_path(str(RUNNER.with_name("configure.py")))["platform_settings"]
        platform = dict(name="simple-spike-rv32max", ram_origin=0x90000000,
                        ram_bytes=0x100000, test_base=0x90001000,
                        access_fault_address=0, access_fault_bytes=4096)
        projected = settings(platform, platform["name"])
        self.assertEqual(projected["ram_origin"], 0x90000000)
        self.assertEqual(projected["test_base"], 0x90001000)
        with self.assertRaises(ValueError):
            settings(platform, "simple-rv5stage-rv32max")
        for change in (dict(test_base=0), dict(ram_bytes=0), dict(ram_origin=-1),
                       dict(ram_bytes=4097), dict(test_base=True)):
            with self.subTest(change=change), self.assertRaises(ValueError):
                settings({**platform, **change}, platform["name"])

    def test_rv32_integer_vector_projection(self):
        configure = runpy.run_path(str(RUNNER.with_name("configure.py")))
        udb = vector_udb()
        udb["implemented_extensions"] = [entry for entry in udb["implemented_extensions"]
                                         if entry["name"] in {"Sm", "Zve32x", "Zvl32b", "Zvl64b", "Zvbb", "Zvkb", "Zvkt"}]
        for sfence_illegal in (False, True):
            udb["params"].update(MXLEN=32, VLEN=64, ELEN=32, PHYS_ADDR_WIDTH=32,
                                 TRAP_ON_SFENCE_VMA_WHEN_SATP_MODE_IS_READ_ONLY=sfence_illegal,
                                 VECTOR_LS_INDEX_MAX_EEW="32", MSTATUS_FS_LEGAL_VALUES=[0])
            config = configure["sail_config"](sail_default(), udb, 0x80000000, 0x40000000)
            self.assertEqual(config["base"]["xlen"], 32)
            self.assertEqual(config["memory"]["physaddr_bits"], 32)
            vector = config["extensions"]["V"]
            self.assertEqual((vector["support_level"], vector["vlen_exp"], vector["elen_exp"], vector["max_index_eew_exp"]),
                             ("Integer", 6, 5, 5))
            self.assertFalse(config["extensions"]["F"]["supported"])
            self.assertFalse(config["extensions"]["D"]["supported"])
            self.assertEqual(config["extensions"]["Svbare"]["sfence_vma_illegal_if_svbare_only"], sfence_illegal)

        bounds = {"Za64rs": "1.0.0"}
        configure["validate_reservation_bounds"]({"reservation_set_size_exp": 2}, bounds, 32)
        with self.assertRaises(ValueError):
            configure["validate_reservation_bounds"]({"reservation_set_size_exp": 1}, bounds, 32)
        udb["params"]["VECTOR_LS_INDEX_MAX_EEW"] = "64"
        with self.assertRaises(ValueError):
            configure["sail_config"](sail_default(), udb, 0x80000000, 0x40000000)

    def test_rv32_single_precision_vector_projection(self):
        configure = runpy.run_path(str(RUNNER.with_name("configure.py")))
        udb = vector_udb()
        udb["implemented_extensions"] = [entry for entry in udb["implemented_extensions"]
                                         if entry["name"] in {"Sm", "Zve32x", "Zve32f", "Zvl32b", "Zvl64b", "Zvbb", "Zvkb", "Zvkt"}]
        udb["implemented_extensions"].append({"name": "F", "version": "= 2.2.0"})
        udb["params"].update(MXLEN=32, VLEN=64, ELEN=32, PHYS_ADDR_WIDTH=32,
                             VECTOR_LS_INDEX_MAX_EEW="32", MSTATUS_FS_LEGAL_VALUES=[0, 1, 2, 3])
        config = configure["sail_config"](sail_default(), udb, 0x80000000, 0x40000000)
        vector = config["extensions"]["V"]
        self.assertEqual((vector["support_level"], vector["vlen_exp"], vector["elen_exp"]),
                         ("Float_single", 6, 5))
        self.assertEqual(config["base"]["xlen"], 32)
        self.assertTrue(config["extensions"]["F"]["supported"])
        self.assertFalse(config["extensions"]["D"]["supported"])

    def test_spike_pmp_projection_preserves_entry_count_and_granularity(self):
        configure = runpy.run_path(str(RUNNER.with_name("configure.py")))
        params = architecture_params(asid_width=16)
        params.update(NUM_PMP_ENTRIES=16, NUM_USABLE_PMP_ENTRIES=16, PMP_GRANULARITY=2,
                      PMP_NA4_SUPPORTED=True, PMP_NAPOT_SUPPORTED=True, PMP_TOR_SUPPORTED=True,
                      MCOUNTENABLE_EN=[True] * 3 + [False] * 29,
                      SCOUNTENABLE_EN=[True] * 3 + [False] * 29,
                      MARCHID_IMPLEMENTED=True, ARCH_ID_VALUE=5,
                      MIMPID_IMPLEMENTED=True, IMP_ID_VALUE=7,
                      VENDOR_ID_BANK=2, VENDOR_ID_OFFSET=3)
        udb = {"params": params, "implemented_extensions": [
            {"name": "Sm", "version": "= 1.13.0"},
            {"name": "Zihpm", "version": "= 2.0"},
        ]}
        config = configure["sail_config"](sail_default(), udb, 0x80000000, 0x40000000)
        self.assertEqual(config["memory"]["pmp"], {
            "count": 16, "usable_count": 16, "grain": 0, "na4_supported": True,
            "napot_supported": True, "tor_supported": True,
        })
        self.assertEqual(config["memory"]["asidlen"], 16)
        self.assertEqual(config["platform"]["archid"], 5)
        self.assertEqual(config["platform"]["impid"], 7)
        self.assertEqual(config["platform"]["vendorid"], (2 << 7) | 3)
        self.assertEqual(config["base"]["privileged_isa_version"], "Privileged_ISA_1_13")
        self.assertIs(config["extensions"]["Zihpm"]["supported"], True)
        self.assertEqual(config["base"]["writable_hpm_counters"], {"len": 32, "value": "0x0"})
        self.assertEqual(config["base"]["mcounteren_writable_bits"], {"len": 32, "value": "0x7"})
        self.assertEqual(config["base"]["scounteren_writable_bits"], {"len": 32, "value": "0x7"})
        self.assertEqual(int(config["base"]["medeleg"]["delegatable_bits"]["value"], 0), 0x8b3ff)

    def test_configuration_includes_privileged_tests(self):
        spec = importlib.util.spec_from_file_location("act_configure", RUNNER.with_name("configure.py"))
        configure = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(configure)
        config = configure.test_config("simple-rv5stage-rva23", "gcc", "objdump", "/tmp/sail", "/tmp/udb.yaml")
        self.assertIs(config["include_priv_tests"], True)
        self.assertEqual(config["udb_config"], str(Path("/tmp/udb.yaml").resolve()))

    def test_wait_on_reservation_policy_follows_the_profile(self):
        configure = runpy.run_path(str(RUNNER.with_name("configure.py")))
        for is_nop in (False, True):
            default = sail_default()
            default["extensions"]["Zawrs"] = {
                "supported": False, "nto": {"is_nop": not is_nop}, "sto": {"is_nop": not is_nop}}
            params = architecture_params()
            params["ZAWRS_NTO_IS_NOP"] = is_nop
            udb = {"params": params, "implemented_extensions": [
                {"name": "Sm", "version": "= 1.13.0"}, {"name": "Zawrs", "version": "= 1.0.0"}]}
            config = configure["sail_config"](default, udb, 0x80000000, 0x40000000)
            self.assertIs(config["extensions"]["Zawrs"]["nto"]["is_nop"], is_nop)
            self.assertIs(config["extensions"]["Zawrs"]["sto"]["is_nop"], is_nop)

    def test_software_check_delegation_requires_a_cfi_extension(self):
        configure = runpy.run_path(str(RUNNER.with_name("configure.py")))
        for extension in ("Zicfilp", "Zicfiss"):
            udb = {"params": architecture_params(), "implemented_extensions": [
                {"name": "Sm", "version": "= 1.13.0"},
                {"name": extension, "version": "= 1.0.0"},
            ]}
            with self.subTest(extension=extension):
                config = configure["sail_config"](sail_default(), udb, 0x80000000, 0x40000000)
                self.assertEqual(int(config["base"]["medeleg"]["delegatable_bits"]["value"], 0), 0xcb3ff)

    def test_architecture_settings_come_from_core_profile(self):
        spec = importlib.util.spec_from_file_location("act_configure", RUNNER.with_name("configure.py"))
        configure = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(configure)
        architectures = ((0, "= 1.12.0", True), (7, "1.12", False), (16, "1.11", False))
        cache_blocks = ((None, None), ("Zicboz", 32), ("Zicboz", 64), ("Zicbom", 128), ("Zicbop", 16),
                        ("Zic64b", 64), ("Zic64b", 32), ("Zic64b", 128),
                        ("Zicboz", 0), ("Zicboz", -64), ("Zicboz", 48), ("Zicboz", True))
        for (width, version, svade), (cache_extension, block_size) in product(architectures, cache_blocks):
            with self.subTest(asid_width=width, privileged_version=version, svade=svade,
                              cache_extension=cache_extension, block_size=block_size):
                params = architecture_params(width)
                default = sail_default()
                extensions = [{"name": "Sm", "version": version},
                              {"name": "Za64rs", "version": "= 1.0.0"},
                              {"name": "Za128rs", "version": "1.0.0"}]
                if svade:
                    extensions.append({"name": "Svade", "version": "= 1.0.0"})
                if cache_extension:
                    extensions.append({"name": cache_extension, "version": "= 1.0.0"})
                    params["CACHE_BLOCK_SIZE"] = block_size
                udb = {"params": params, "implemented_extensions": extensions}
                if cache_extension and (type(block_size) is not int or block_size <= 0 or block_size & (block_size - 1)
                                        or (cache_extension == "Zic64b" and block_size != 64)):
                    with self.assertRaisesRegex(ValueError, "CACHE_BLOCK_SIZE"):
                        configure.sail_config(default, udb, 0x80000000, 0x40000000)
                    continue
                config = configure.sail_config(default, udb, 0x80000000, 0x40000000)
                self.assertEqual(config["platform"]["reservation"]["reservation_set_size_exp"], 3)
                self.assertIs(config["platform"]["reservation"]["require_exact_reservation_addr"], True)
                io, ram = config["memory"]["regions"]
                self.assertIs(ram["attributes"]["supports_cbo_zero"], cache_extension == "Zicboz")
                self.assertIs(io["attributes"]["supports_cbo_zero"], False)
                self.assertEqual(config["platform"]["cache_block_size_exp"],
                                 block_size.bit_length() - 1 if cache_extension else 9)
                self.assertEqual(config["memory"]["asidlen"], width)
                self.assertEqual(config["base"]["privileged_isa_version"],
                                 "Privileged_ISA_1_11" if version == "1.11" else "Privileged_ISA_1_12")
                self.assertIs(config["extensions"]["Svade"]["supported"], svade)
                self.assertEqual(config["memory"]["misaligned"]["exceptions"]["lrsc"],
                                 {"Some": "AlignmentException"})
                self.assertEqual(int(config["base"]["medeleg"]["delegatable_bits"]["value"], 0), 0x8b3ff)

    def test_vector_settings_come_from_core_profile(self):
        configure = runpy.run_path(str(RUNNER.with_name("configure.py")))
        config = configure["sail_config"](sail_default(), vector_udb(), 0x80000000, 0x40000000)
        vector = config["extensions"]["V"]
        self.assertEqual(vector["support_level"], "Full")
        self.assertEqual(vector["vlen_exp"], 7)
        self.assertEqual(vector["elen_exp"], 6)
        self.assertEqual(vector["max_index_eew_exp"], 6)
        self.assertIs(vector["vl_use_ceil"], False)
        self.assertEqual(vector["reserved_behavior"], {
            "illegal_vtype": "IllegalVtype_SetVill", "vstart_out_of_bounds": "Vstart_Ignore",
        })
        self.assertEqual(vector["vstart"]["zero_required"], {"arith": False, "scalar_move": False})
        self.assertEqual(config["base"]["mstatus"]["vs_legal_states"], "ExtContext_FourState")
        for name in ("Zvfh", "Zvkb", "Zvbb", "Zvkt"):
            self.assertIs(config["extensions"][name]["supported"], True)

    def test_spike_vector_choices_and_reference_differences_are_preserved(self):
        configure = runpy.run_path(str(RUNNER.with_name("configure.py")))
        udb = vector_udb()
        udb["params"].update(RESERVED_VSET_X0X0_VILL_SET="always",
                             RESERVED_VSET_X0X0_VLMAX_CHANGE="always",
                             VFREDUSUM_NAN="custom", ZAWRS_NTO_IS_NOP=True)
        udb["implemented_extensions"].append({"name": "Zawrs", "version": "= 1.0.0"})
        default = sail_default()
        default["extensions"]["Zawrs"] = {"supported": False, "nto": {}, "sto": {}}
        config = configure["sail_config"](default, udb, 0x80000000, 0x40000000)
        self.assertEqual(config["extensions"]["V"]["support_level"], "Full")
        self.assertTrue(config["extensions"]["Zawrs"]["nto"]["is_nop"])
        self.assertEqual(configure["reference_model_differences"](udb["params"]),
                         {"VFREDUSUM_NAN": {"dut": "custom", "sail": "no_change"}})
        self.assertEqual(udb["params"]["VFREDUSUM_NAN"], "custom")
        self.assertEqual(set(configure["reference_model_differences"](vector_udb()["params"])),
                         {"RESERVED_VSET_X0X0_VILL_SET", "RESERVED_VSET_X0X0_VLMAX_CHANGE"})

    def test_pointer_masking_environment_projects_to_sail_hardware(self):
        configure = runpy.run_path(str(RUNNER.with_name("configure.py")))
        for pmlen in (0, 7, 16):
            params = architecture_params()
            params["PMLEN"] = pmlen
            udb = {"params": params, "implemented_extensions": [
                {"name": "Sm", "version": "= 1.12.0"},
                {"name": "Ssnpm", "version": "= 1.0.0"},
                {"name": "Supm", "version": "= 1.0.0"},
            ]}
            config = configure["sail_config"](sail_default(), udb, 0x80000000, 0x40000000)
            self.assertEqual(config["extensions"]["Ssnpm"], {
                "supported": True, "supported_pmlen_7": True, "supported_pmlen_16": True,
            })
        for extensions, message in ((["Supm"], "requires Ssnpm"),
                                    (["Ssnpm", "Supm"], "needs a Sail mapping")):
            params = architecture_params()
            params["PMLEN"] = 7
            invalid = {"params": params, "implemented_extensions": [
                {"name": "Sm", "version": "= 1.12.0"},
            ] + [{"name": name, "version": "= 2.0.0" if len(extensions) == 2 else "= 1.0.0"}
                 for name in extensions]}
            with self.subTest(extensions=extensions), self.assertRaisesRegex(ValueError, message):
                configure["sail_config"](sail_default(), invalid, 0x80000000, 0x40000000)
        for extensions, pmlen, message in ((["Ssnpm"], None, "requires PMLEN"),
                                           (["Ssnpm"], 8, "PMLEN to be 0, 7, or 16"),
                                           ([], 7, "PMLEN requires Ssnpm")):
            params = architecture_params()
            if pmlen is not None:
                params["PMLEN"] = pmlen
            invalid = {"params": params, "implemented_extensions": [
                {"name": "Sm", "version": "= 1.12.0"},
            ] + [{"name": name, "version": "= 1.0.0"} for name in extensions]}
            with self.subTest(extensions=extensions, pmlen=pmlen), self.assertRaisesRegex(ValueError, message):
                configure["sail_config"](sail_default(), invalid, 0x80000000, 0x40000000)

    def test_access_fault_region_is_unmapped_sized_and_rendered(self):
        configure = runpy.run_path(str(RUNNER.with_name("configure.py")))
        udb = vector_udb()
        config = configure["sail_config"](sail_default(), udb, 0x80000000, 0x40000000)
        validate = configure["validate_access_fault_region"]
        validate(config, udb["params"], 0, 0x1000)
        rendered = configure["render_rvmodel_macros"](
            "before\n// @RVMODEL_ACCESS_FAULT_ADDRESS@\nafter\n", 0
        )
        self.assertEqual(rendered, "before\n#define RVMODEL_ACCESS_FAULT_ADDRESS 0x0\nafter\n")
        self.assertEqual(
            configure["render_rvmodel_macros"]("// @RVMODEL_ACCESS_FAULT_ADDRESS@\n", None), "\n"
        )
        for address, size, message in (
            (0, None, "provided together"),
            (0, 127, "at least 128 bytes"),
            (0x02000000, 0x1000, "overlaps"),
            (0x80000000, 0x1000, "overlaps"),
            (1 << 44, 0x1000, "physical address width"),
        ):
            with self.subTest(address=address, size=size), self.assertRaisesRegex(ValueError, message):
                validate(config, udb["params"], address, size)

    def test_single_core_rv5stage_soc_timer_macros_match_aclint(self):
        macros = RVMODEL_MACROS.read_text()
        for definition in (
            "#define RVMODEL_MTIMECMP_ADDRESS 0x02004000",
            "#define RVMODEL_MTIME_ADDRESS 0x0200bff8",
            "#define RVMODEL_MAX_CYCLES_PER_TIMER_TICK 1",
            "#define RVMODEL_TIMER_INT_SOON_DELAY 5000",
        ):
            self.assertIn(definition, macros)

    def test_vector_projection_rejects_inconsistent_profiles(self):
        configure = runpy.run_path(str(RUNNER.with_name("configure.py")))
        project = configure["sail_config"]
        cases = []
        invalid_vlen = vector_udb()
        invalid_vlen["params"]["VLEN"] = 96
        cases.append((invalid_vlen, "VLEN must be a positive power of two"))
        missing_closure = vector_udb()
        missing_closure["implemented_extensions"] = [
            extension for extension in missing_closure["implemented_extensions"]
            if extension["name"] != "Zve64d"
        ]
        cases.append((missing_closure, "V UDB closure is missing"))
        missing_length = vector_udb()
        missing_length["implemented_extensions"] = [
            extension for extension in missing_length["implemented_extensions"]
            if extension["name"] != "Zvl128b"
        ]
        cases.append((missing_length, "VLEN=128 requires"))
        missing_zvkb = vector_udb()
        missing_zvkb["implemented_extensions"] = [
            extension for extension in missing_zvkb["implemented_extensions"]
            if extension["name"] != "Zvkb"
        ]
        cases.append((missing_zvkb, "Zvbb UDB closure is missing Zvkb"))
        stateen = vector_udb()
        stateen["implemented_extensions"].append({"name": "Smstateen", "version": "= 1.0.0"})
        cases.append((stateen, "state-enable configurations"))
        wrong_behavior = vector_udb()
        wrong_behavior["params"]["VECTOR_LS_MISALIGNED_LEGAL"] = True
        cases.append((wrong_behavior, "VECTOR_LS_MISALIGNED_LEGAL=False"))
        for udb, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                project(sail_default(), udb, 0x80000000, 0x40000000)

    def test_reservation_guarantees_validate_sail_platform(self):
        configure = runpy.run_path(str(RUNNER.with_name("configure.py")))
        validate = configure["validate_reservation_bounds"]
        for names, maximum in ((["Za64rs"], 6), (["Za128rs"], 7), (["Za64rs", "Za128rs"], 6)):
            for size_exp in (3, 6, 7, 8, 2, -1, True, "3"):
                with self.subTest(names=names, size_exp=size_exp):
                    reservation = {"reservation_set_size_exp": size_exp}
                    extensions = dict.fromkeys(names, "1.0.0")
                    if type(size_exp) is int and 3 <= size_exp <= maximum:
                        validate(reservation, extensions, 64)
                        self.assertEqual(reservation["reservation_set_size_exp"], size_exp)
                    else:
                        with self.assertRaisesRegex(ValueError, "reservation size"):
                            validate(reservation, extensions, 64)
            with self.assertRaisesRegex(ValueError, "version"):
                validate({"reservation_set_size_exp": 3}, dict.fromkeys(names, "2.0.0"), 64)
        validate({"reservation_set_size_exp": 12}, {}, 64)


class ArchTestGenerationTest(unittest.TestCase):
    def test_make_rejects_cross_product_configuration_before_generation(self):
        result = subprocess.run(
            ["make", "arch-test-config", "SOC=simple-spike-rv32max",
             "ACT_CONFIGURATION=simple-rv5stage-rv32max"],
            cwd=RUNNER.parents[1], capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must match the selected", result.stdout + result.stderr)

    def test_shards_cover_inventory_exactly_once_and_replace_stale_links(self):
        spec = importlib.util.spec_from_file_location('act_shard', RUNNER.with_name('shard.py'))
        sharder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(sharder)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            elfs = root / 'elfs'
            (elfs / 'nested').mkdir(parents=True)
            for index in range(11):
                (elfs / 'nested' / f'{index}.elf').touch()
            partitions = [sharder.partition(elfs, root / f'shard-{i}/elfs', i, 4) for i in range(4)]
            combined = [elf for group in partitions for elf in group]
            self.assertEqual(len(combined), len(set(combined)))
            self.assertEqual(set(combined), set(elfs.resolve().rglob('*.elf')))
            (elfs / 'nested/0.elf').unlink()
            sharder.partition(elfs, root / 'shard-0/elfs', 0, 4)
            self.assertFalse((root / 'shard-0/elfs/nested/0.elf').is_symlink())
            self.assertEqual(len(list(elfs.rglob('*.elf'))), 10)
            foreign = root / 'foreign/elfs'
            foreign.mkdir(parents=True)
            (foreign / 'keep.elf').symlink_to(elfs / 'nested/1.elf')
            with self.assertRaises(ValueError):
                sharder.partition(elfs, foreign, 0, 4)
            self.assertTrue((foreign / 'keep.elf').is_symlink())

    def test_entry_point_preserves_upstream_sail_version_check(self):
        config = ModuleType("act.config")
        config.REQUIRED_SAIL_VERSION = "0.14.1"
        config.check_ref_model_version = Mock()
        version_check = config.check_ref_model_version
        act = ModuleType("act")
        act.config = config
        cli = ModuleType("act.act")
        cli.main = Mock()
        with patch.dict(sys.modules, {"act": act, "act.config": config, "act.act": cli}):
            runpy.run_path(str(RUNNER.with_name("build.py")), run_name="__main__")
        self.assertEqual(config.REQUIRED_SAIL_VERSION, "0.14.1")
        self.assertIs(config.check_ref_model_version, version_check)
        cli.main.assert_called_once_with()

    def test_generates_all_supported_tests_and_replaces_only_elf_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            upstream = root / "upstream"
            source_tests = upstream / "tests"
            source_tests.mkdir(parents=True)
            (source_tests / "handwritten.S").write_text("# Handwritten ACT test retained during staging.\n")
            (upstream / "testplans").mkdir()
            patch_dir = root / "patches"
            patch_dir.mkdir()
            series = patch_dir / "series"
            series.write_text("# No downstream patches are needed by this fixture.\n")
            build_root = root / "build"
            elf_dir = build_root / "work/simple-rv5stage-rva23/simple-rv5stage-rva23/elfs"
            elf_dir.mkdir(parents=True)
            (elf_dir / "old.elf").touch()
            (elf_dir / "old.elf.objdump").touch()
            other_elf = build_root / "work/other/keep.elf"
            other_elf.parent.mkdir(parents=True)
            other_elf.touch()
            act = root / "venv/bin/python"
            act.parent.mkdir(parents=True)
            testgen = root / "venv/bin/testgen"
            testgen.write_text(
                f"#!{sys.executable}\n"
                "# Emulates canonical test generation into the staged ACT test tree.\n"
                "import sys\nfrom pathlib import Path\n"
                "assert Path(sys.argv[1]).name == 'testplans'\n"
                "output = Path(sys.argv[sys.argv.index('-o') + 1])\n"
                "assert (output / 'handwritten.S').is_file()\n"
                "assert sys.argv[sys.argv.index('--extensions') + 1] == 'all'\n"
                "(output / 'vector-generated.S').touch()\n"
            )
            testgen.chmod(0o755)
            act.write_text(
                f"#!{sys.executable}\n"
                "# Emulates ACT generation to check the Make-to-ACT contract.\n"
                "import sys\nfrom pathlib import Path\n"
                "assert Path(sys.argv[1]).name == 'build.py'\n"
                "assert sys.argv[sys.argv.index('--extensions') + 1] == 'all'\n"
                "assert '--keep-going' in sys.argv\n"
                "test_dir = Path(sys.argv[sys.argv.index('--test-dir') + 1])\n"
                "assert (test_dir / 'handwritten.S').is_file()\n"
                "assert (test_dir / 'vector-generated.S').is_file()\n"
                f"elf_dir = Path({str(elf_dir)!r})\n"
                "assert not list(elf_dir.rglob('*.elf'))\n"
                "(elf_dir / 'selected.elf').touch()\n"
            )
            act.chmod(0o755)
            result = subprocess.run(
                ["make", "-o", "arch-test-config", "arch-test-elfs", "ACT_CONFIGURATION=simple-rv5stage-rva23",
                 f"ACT_DIR={upstream}", f"ACT_VENV={root / 'venv'}", f"ACT_BUILD_ROOT={build_root}",
                 f"ACT_PATCH_SERIES={series}"],
                cwd=RUNNER.parents[1], capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual([p.name for p in elf_dir.glob('*.elf')], ["selected.elf"])
            self.assertTrue((elf_dir / "old.elf.objdump").exists())
            self.assertTrue(other_elf.exists())


class ArchTestRunnerTest(unittest.TestCase):
    def test_make_runs_only_its_shard_and_preserves_upstream_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            elfs = root / 'work/simple-rv5stage-rva23/simple-rv5stage-rva23/elfs'
            elfs.mkdir(parents=True)
            for index in range(8):
                (elfs / f'{index}.elf').touch()
            binary = root / 'VTestDriver'
            binary.write_bytes(b'fake native artifact')
            artifact = RUNNER.parents[1] / 'program-test/artifact.py'
            subprocess.run([sys.executable, str(artifact), 'record', '--binary', str(binary), '--soc', 'simple-rv5stage-rva23'], check=True)
            (root / 'run_tests.py').write_text(
                '# Emulates upstream ACT execution for the Make/shard/result contract.\n'
                'import os, sys\nfrom pathlib import Path\n'
                'elfs = Path(sys.argv[-1])\n'
                "names = sorted(path.name for path in elfs.rglob('*.elf'))\n"
                "assert names == ['1.elf', '5.elf'], names\n"
                "summary = ''.join(f'{Path(n).stem}.log  RVCP-SUMMARY: TEST PASSED - Test File \\\"test.S\\\"\\n' for n in names)\n"
                "(elfs.parent / 'summary.log').write_text(summary)\n"
                "sys.exit(int(os.environ.get('FAKE_ACT_EXIT', '0')))\n"
            )
            command = ['make', '-C', str(RUNNER.parents[1]), 'arch-test-run',
                       'ACT_CONFIGURATION=simple-rv5stage-rva23',
                       f'ACT_DIR={root}', f'ACT_BUILD_ROOT={root}', f'ACT_PYTHON={sys.executable}',
                       f'PYTHON={sys.executable}', f'PREBUILT_SIMULATOR={binary}', 'ACT_SHARDS=4', 'ACT_SHARD=1']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue((elfs.parent / 'shards/1/results.json').is_file())
            result = subprocess.run(command, env={**os.environ, 'FAKE_ACT_EXIT': '7'}, capture_output=True)
            self.assertNotEqual(result.returncode, 0)

    def test_report_rejects_missing_results(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            elfs = root / 'elfs'
            elfs.mkdir()
            (elfs / 'complete.elf').touch()
            (elfs / 'missing.elf').touch()
            (root / 'summary.log').write_text('complete.log  RVCP-SUMMARY: TEST PASSED - Test File "complete.S"\n')
            result = subprocess.run([sys.executable, str(RUNNER.with_name('report.py')), str(elfs)], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b'"error": 1', result.stdout)
            self.assertTrue((root / 'junit.xml').is_file())

    def test_report_distinguishes_cycle_timeout_from_target_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'elfs').mkdir()
            (root / 'logs').mkdir()
            (root / 'elfs/limited.elf').touch()
            (root / 'logs/limited.log').write_text('SoC harness simulation timed out\n')
            (root / 'summary.log').write_text('limited.log  RVCP-SUMMARY: TEST FAILED - Test File "limited.S"\n')
            result = subprocess.run([sys.executable, str(RUNNER.with_name('report.py')), str(root / 'elfs')], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b'"timeout": 1', result.stdout)

    def run_simulator(self, output, code):
        with tempfile.TemporaryDirectory(prefix="rhodium-act-test-") as directory:
            root = Path(directory)
            elf = root / "test with spaces.elf"
            elf.touch()
            simulator = root / "fake simulator"
            simulator.write_text(
                f"#!{sys.executable}\n"
                "import sys\n"
                "assert sys.argv[1:4] == ['+permissive', '+max-cycles=123', '+permissive-off']\n"
                "assert sys.argv[4].endswith('test with spaces.elf')\n"
                f"print({output!r})\n"
                f"sys.exit({code})\n"
            )
            simulator.chmod(0o755)
            return subprocess.run(
                [sys.executable, str(RUNNER), "--simulator", str(simulator),
                 "--max-cycles", "123", str(elf)], capture_output=True, text=True,
            )

    def test_confirmed_target_completion(self):
        result = self.run_simulator("SoC harness simulation passed", 0)
        self.assertEqual(result.returncode, 0)
        self.assertIn('RVCP-SUMMARY: TEST PASSED - Test File "test with spaces.S"', result.stdout)

    def test_exit_zero_without_completion_is_failure(self):
        result = self.run_simulator("", 0)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("RVCP-SUMMARY: TEST FAILED", result.stdout)

    def test_failure_status_overrides_pass_message(self):
        result = self.run_simulator("SoC harness simulation passed", 7)
        self.assertEqual(result.returncode, 7)
        self.assertIn("RVCP-SUMMARY: TEST FAILED", result.stdout)

    def test_simulator_failure_is_preserved(self):
        result = self.run_simulator("SoC harness reported target failure", 3)
        self.assertEqual(result.returncode, 3)
        self.assertIn("target failure", result.stdout)

    def test_timeout_is_failure(self):
        result = self.run_simulator("SoC harness simulation timed out", 1)
        self.assertEqual(result.returncode, 1)
        self.assertIn("timed out", result.stdout)


if __name__ == "__main__":
    unittest.main()
