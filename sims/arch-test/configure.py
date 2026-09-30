#!/usr/bin/env python3
# Prepares all UDB-applicable ACT tests and their Sail/platform configuration.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess


def bits(value, width=64):
    return {"len": width, "value": hex(value)}


def encoded_integer(value):
    return int(value["value"], 0)


def platform_settings(platform, name):
    """Check the generated platform identity and payload memory contract."""
    if platform["name"] != name:
        raise ValueError("ACT platform must match the selected product")
    settings = {key: platform[key] for key in (
        "ram_origin", "ram_bytes", "test_base", "access_fault_address", "access_fault_bytes"
    )}
    if any(type(value) is not int or value < 0 for value in settings.values()):
        raise ValueError("ACT platform addresses and sizes must be natural integers")
    origin, size, entry = (settings[key] for key in ("ram_origin", "ram_bytes", "test_base"))
    if size == 0 or not origin <= entry < origin + size:
        raise ValueError("test entry must lie in a nonempty RAM window")
    if origin % 4096 or size % 4096:
        raise ValueError("Sail RAM regions must be page aligned")
    return settings


def validate_access_fault_region(config, params, address, size):
    """Validate an optional platform hole used by architectural fault tests."""
    if (address is None) != (size is None):
        raise ValueError("access-fault address and size must be provided together")
    if address is None:
        return
    if type(address) is not int or address < 0 or type(size) is not int or size <= 0:
        raise ValueError("access-fault region must have a natural address and positive size")
    required = max(128, math.ceil(2 * params.get("VLEN", 0) / 8))
    if size < required:
        raise ValueError(f"access-fault region must contain at least {required} bytes")
    limit = 1 << params["PHYS_ADDR_WIDTH"]
    end = address + size
    if end > limit:
        raise ValueError("access-fault region must fit the physical address width")
    for region in config["memory"]["regions"]:
        region_base = encoded_integer(region["base"])
        region_end = region_base + encoded_integer(region["size"])
        if address < region_end and region_base < end:
            raise ValueError("access-fault region overlaps a modeled Sail memory region")


def render_rvmodel_macros(template, access_fault_address):
    """Render the DUT macro header with its optional platform fault address."""
    marker = "// @RVMODEL_ACCESS_FAULT_ADDRESS@"
    if template.count(marker) != 1:
        raise ValueError("rvmodel macro template must contain one access-fault marker")
    definition = "" if access_fault_address is None else (
        f"#define RVMODEL_ACCESS_FAULT_ADDRESS {hex(access_fault_address)}"
    )
    return template.replace(marker, definition)


RESERVATION_BOUNDS = {"Za64rs": 6, "Za128rs": 7}
VECTOR_BASE_VERSIONS = {
    "V": "1.0.0",
    "Zve32x": "1.0.0",
    "Zve32f": "1.0.0",
    "Zve64x": "1.0.0",
    "Zve64f": "1.0.0",
    "Zve64d": "1.0.0",
}
VECTOR_CLOSURES = {
    "V": {"V", "Zve32x", "Zve32f", "Zve64x", "Zve64f", "Zve64d"},
    "Zve64d": {"Zve32x", "Zve32f", "Zve64x", "Zve64f", "Zve64d"},
    "Zve64f": {"Zve32x", "Zve32f", "Zve64x", "Zve64f"},
    "Zve64x": {"Zve32x", "Zve64x"},
    "Zve32f": {"Zve32x", "Zve32f"},
    "Zve32x": {"Zve32x"},
}
VECTOR_PARAMETER_VALUES = {
    "FOLLOW_VTYPE_RESET_RECOMMENDATION": True,
    "IMPRECISE_VECTOR_TRAP_SETTABLE": False,
    "LEGAL_VSTART": "1_stride",
    "RVV_VL_WHEN_AVL_LT_DOUBLE_VLMAX": "VLMAX",
    "SUPPORT_FRACTIONAL_LMUL_BEYOND_REQUIRED": "no_unrequired_supported",
    "VECTOR_FF_NO_EXCEPTION_TRIM": False,
    "VECTOR_FF_SEG_EXCEPTION_PARTIAL_LOAD": "custom",
    "VECTOR_FF_UPDATE_PAST_TRIM": "update_none",
    "VECTOR_LOAD_PAST_TRAP": False,
    "VECTOR_LOAD_SEG_FF_OVERWRITE_ELEMENTS_AFTER_FAULT": "no_overwrite",
    "VECTOR_LS_SEG_PARTIAL_ACCESS": True,
    "VECTOR_LS_WHOLEREG_MISALIGNED_LEGAL": False,
    "VFREDUSUM_FINAL_NODE_ELEMENT_BEHAVIOR": "copy",
    "VFREDUSUM_INACTIVE_NODE_ELEMENT_BEHAVIOR": "copy",
    "VFREDUSUM_NODE_ROUNDING_BEHAVIOR": "SEW_precision",
}
POINTER_MASKING_VERSIONS = {"Ssnpm": "1.0.0", "Supm": "1.0.0"}


def reference_model_differences(params):
    """Report legal DUT choices that Sail 0.14.1 cannot configure exactly.

    These do not change the selected ISA or suppress tests. Preserve the DUT's
    UDB parameters and expose reference-model differences with every ACT build.
    """
    fixed = {"RESERVED_VSET_X0X0_VILL_SET": "always",
             "RESERVED_VSET_X0X0_VLMAX_CHANGE": "always",
             "VFREDUSUM_NAN": "no_change"}
    differences = {name: {"dut": params[name], "sail": value}
                   for name, value in fixed.items() if name in params and params[name] != value}
    if any(params.get("HPM_COUNTER_EN", [])[3:]):
        differences["HPM_EVENTS"] = {
            "dut": params.get("HPM_EVENTS", "implementation-defined"),
            "sail": "selector writes retained; no event increments or generated overflow",
        }
    if "COUNTINHIBIT_EN" in params:
        sail_mask = [i in (0, 2) or (i >= 3 and enabled)
                     for i, enabled in enumerate(params["HPM_COUNTER_EN"])]
        if params["COUNTINHIBIT_EN"] != sail_mask:
            differences["COUNTINHIBIT_EN"] = {"dut": params["COUNTINHIBIT_EN"], "sail": sail_mask}
    return differences


def validate_reservation_bounds(reservation, extensions, xlen):
    # Sail 0.14.1 has naturally aligned, fixed-size reservation sets, not switches
    # for these guarantees. Retain its chosen size; the extensions are bounds,
    # not requests to enlarge reservations to a cache line.
    for name, maximum_exp in RESERVATION_BOUNDS.items():
        if name not in extensions:
            continue
        if extensions[name] != "1.0.0":
            raise ValueError(f"{name} needs a Sail mapping for version {extensions[name]}")
        size_exp = reservation["reservation_set_size_exp"]
        minimum_exp = (xlen // 8).bit_length() - 1
        if type(size_exp) is not int or not minimum_exp <= size_exp <= maximum_exp:
            raise ValueError(f"{name} requires a Sail reservation size between {xlen // 8} and {1 << maximum_exp} bytes")


def power_of_two_exp(name, value):
    if type(value) is not int or value <= 0 or value & (value - 1):
        raise ValueError(f"{name} must be a positive power of two")
    exponent = value.bit_length() - 1
    if not 3 <= exponent <= 16:
        raise ValueError(f"{name} must be between 8 and 65536")
    return exponent


def project_vector(model_extensions, extensions, params):
    """Project UDB's vector profile and implied extension closure into Sail."""
    names = extensions.keys()
    selected = names & VECTOR_BASE_VERSIONS.keys()
    zvl = {name for name in names if re.fullmatch(r"Zvl[0-9]+b", name)}
    vector = model_extensions["V"]
    if not selected:
        if zvl:
            raise ValueError("Zvl extensions require a Zve or V profile")
        vector["support_level"] = "Disabled"
        return set()
    for name in selected:
        if extensions[name] != VECTOR_BASE_VERSIONS[name]:
            raise ValueError(f"{name} needs a Sail mapping for version {extensions[name]}")
    profile = next(name for name in VECTOR_CLOSURES if name in selected)
    missing = VECTOR_CLOSURES[profile] - selected
    if missing:
        raise ValueError(f"{profile} UDB closure is missing {sorted(missing)}")
    vlen_exp = power_of_two_exp("VLEN", params.get("VLEN"))
    elen_exp = power_of_two_exp("ELEN", params.get("ELEN"))
    if params.get("SEW_MIN") != 8:
        raise ValueError("Sail vector projection requires SEW_MIN=8")
    if profile == "V" and vlen_exp < 7:
        raise ValueError("V requires VLEN of at least 128 bits")
    if profile in {"V", "Zve64x", "Zve64f", "Zve64d"} and elen_exp < 6:
        raise ValueError(f"{profile} requires ELEN of at least 64 bits")
    if profile in {"Zve32x", "Zve32f"} and elen_exp < 5:
        raise ValueError(f"{profile} requires ELEN of at least 32 bits")
    required_zvl = {f"Zvl{1 << exponent}b" for exponent in range(5, vlen_exp + 1)}
    missing_zvl = required_zvl - zvl
    if missing_zvl:
        raise ValueError(f"VLEN={params['VLEN']} requires {sorted(missing_zvl)}")
    for name in zvl:
        length = int(name[3:-1])
        if extensions[name] != "1.0.0":
            raise ValueError(f"{name} needs a Sail mapping for version {extensions[name]}")
        if length <= 0 or length & (length - 1) or length > params["VLEN"]:
            raise ValueError(f"{name} is inconsistent with VLEN={params['VLEN']}")
    if "Zvbb" in names and "Zvkb" not in names:
        raise ValueError("Zvbb UDB closure is missing Zvkb")
    vill = params.get("VILL_SET_ON_RESERVED_VTYPE")
    if type(vill) is not bool:
        raise ValueError("VILL_SET_ON_RESERVED_VTYPE must be Boolean")
    if params.get("HW_MSTATUS_VS_DIRTY_UPDATE") != "precise":
        raise ValueError("Sail vector projection requires precise VS dirty updates")
    for name, expected in VECTOR_PARAMETER_VALUES.items():
        if params.get(name) != expected:
            raise ValueError(f"Sail vector projection requires {name}={expected!r}")
    if params.get("VSSTATUS_VS_EXISTS") != ("H" in names):
        raise ValueError("VSSTATUS_VS_EXISTS must match H support")
    if params.get("VECTOR_LS_MISALIGNED_LEGAL") is not ("Zicclsm" in extensions):
        raise ValueError("VECTOR_LS_MISALIGNED_LEGAL must match Zicclsm support")
    for name, choices in {
        "RESERVED_VSET_X0X0_VILL_SET": ("never", "always"),
        "RESERVED_VSET_X0X0_VLMAX_CHANGE": ("never", "always"),
        "VFREDUSUM_NAN": ("no_change", "custom"),
    }.items():
        if params.get(name) not in choices:
            raise ValueError(f"unmodeled vector parameter {name}={params.get(name)!r}")
    vector["support_level"] = {
        "V": "Full",
        "Zve64d": "Float_double",
        "Zve64f": "Float_single",
        "Zve32f": "Float_single",
        "Zve64x": "Integer",
        "Zve32x": "Integer",
    }[profile]
    vector["vlen_exp"] = vlen_exp
    vector["elen_exp"] = elen_exp
    max_index_eew = params["MXLEN"] if params["VECTOR_LS_INDEX_MAX_EEW"] == "XLEN" else int(params["VECTOR_LS_INDEX_MAX_EEW"])
    if max_index_eew not in (8, 16, 32, 64) or max_index_eew > params["ELEN"]:
        raise ValueError("VECTOR_LS_INDEX_MAX_EEW must be a supported width no greater than ELEN")
    vector["max_index_eew_exp"] = power_of_two_exp("VECTOR_LS_INDEX_MAX_EEW", max_index_eew)
    vector["vl_use_ceil"] = False
    vector["reserved_behavior"]["illegal_vtype"] = "IllegalVtype_SetVill" if vill else "IllegalVtype_Illegal"
    vector["reserved_behavior"]["vstart_out_of_bounds"] = "Vstart_Ignore"
    vector["vstart"]["zero_required"].update(arith=False, scalar_move=False)
    return selected | zvl


def project_pointer_masking(model_extensions, extensions, params):
    """Project the Supm environment claim onto Sail's concrete Ssnpm model."""
    selected = extensions.keys() & POINTER_MASKING_VERSIONS.keys()
    for name in selected:
        if extensions[name] != POINTER_MASKING_VERSIONS[name]:
            raise ValueError(f"{name} needs a Sail mapping for version {extensions[name]}")
    if "Supm" in selected and "Ssnpm" not in selected:
        raise ValueError("Supm with supervisor mode requires Ssnpm")
    if "Ssnpm" in selected:
        pmlen = params.get("PMLEN")
        if type(pmlen) is not int or pmlen not in (0, 7, 16):
            raise ValueError("Ssnpm requires PMLEN to be 0, 7, or 16")
        snpm = model_extensions["Ssnpm"]
        snpm.update(supported=True, supported_pmlen_7=True, supported_pmlen_16=True)
    elif "PMLEN" in params:
        raise ValueError("PMLEN requires Ssnpm")
    return selected


def sail_config(default, udb, origin, size):
    """Project modeled UDB settings; surface remaining model/platform gaps in ACT."""
    params = udb["params"]
    extensions = {entry["name"]: str(entry["version"]).removeprefix("= ") for entry in udb["implemented_extensions"]}
    if params["MXLEN"] not in (32, 64):
        raise ValueError("ACT adapter requires RV32 or RV64")
    pmp_count = params["NUM_PMP_ENTRIES"]
    pmp_usable_count = params.get("NUM_USABLE_PMP_ENTRIES", pmp_count)
    pmp_granularity = params.get("PMP_GRANULARITY", 2)
    if pmp_count not in (0, 16, 64) or type(pmp_granularity) is not int or pmp_granularity < 2:
        raise ValueError("Sail PMP projection requires 0, 16, or 64 entries and at least four-byte granularity")
    if type(pmp_usable_count) is not int or not 0 <= pmp_usable_count <= pmp_count:
        raise ValueError("Sail PMP usable entries must fit the implemented PMP entries")
    if params["MISALIGNED_LDST_EXCEPTION_PRIORITY"] != "high":
        raise ValueError("ACT adapter requires high-priority misaligned exceptions")
    misaligned = params["MISALIGNED_LDST"]
    if type(misaligned) is not bool or misaligned != ("Zicclsm" in extensions):
        raise ValueError("MISALIGNED_LDST must match Zicclsm support")
    if "Zicclsm" in extensions and extensions["Zicclsm"] != "1.0.0":
        raise ValueError("Zicclsm needs a Sail mapping for its advertised version")
    if params["M_MODE_ENDIANNESS"] != "little":
        raise ValueError("initial ACT adapter requires little-endian M mode")
    # Sail fixes user XLEN to base.xlen; Ssu64xl has no separate switch.
    if "Ssu64xl" in extensions:
        if extensions["Ssu64xl"] != "1.0.0" or params["MXLEN"] != 64 or params.get("UXLEN") != [64]:
            raise ValueError("Ssu64xl 1.0.0 requires the fixed RV64 user execution projection")
    model_extensions = default["extensions"]
    vector_extensions = project_vector(model_extensions, extensions, params)
    pointer_masking_extensions = project_pointer_masking(model_extensions, extensions, params)
    unknown = extensions.keys() - model_extensions.keys() - {"I", "C", "Sm", "Smstateen", "Ssstateen", "Ssu64xl"} - RESERVATION_BOUNDS.keys() - vector_extensions - pointer_masking_extensions
    if unknown:
        raise ValueError(f"extensions need Sail mapping: {sorted(unknown)}")
    for name, options in model_extensions.items():
        if "supported" in options:
            options["supported"] = name in extensions
    if "TRAP_ON_SFENCE_VMA_WHEN_SATP_MODE_IS_READ_ONLY" in params:
        model_extensions["Svbare"]["sfence_vma_illegal_if_svbare_only"] = params["TRAP_ON_SFENCE_VMA_WHEN_SATP_MODE_IS_READ_ONLY"]
    if "Zawrs" in extensions:
        model_extensions["Zawrs"]["nto"]["is_nop"] = params["ZAWRS_NTO_IS_NOP"]
        model_extensions["Zawrs"]["sto"]["is_nop"] = params["ZAWRS_NTO_IS_NOP"]
    for name in ("Smstateen", "Ssstateen"):
        if name in extensions and extensions[name] != "1.0.0":
            raise ValueError(f"{name} needs a Sail mapping for version {extensions[name]}")
        model_extensions["Stateen"][name]["supported"] = name in extensions
    if "Smstateen" in extensions:
        if params.get("MSTATEEN_ENVCFG_TYPE") != "rw":
            raise ValueError("Sail state-enable projection requires writable MSTATEEN.ENVCFG")
        if "H" in extensions and params.get("HSTATEEN_ENVCFG_TYPE") != "rw":
            raise ValueError("Sail state-enable projection requires writable HSTATEEN.ENVCFG")
        model_extensions["Stateen"]["SE0_readonly_zero"] = False
    base = default["base"]
    base["xlen"] = params["MXLEN"]
    base["E"] = False
    base["writable_misa"] = any(value for key, value in params.items() if key.startswith("MUTABLE_MISA_"))
    base["privileged_isa_version"] = "Privileged_ISA_" + "_".join(str(extensions["Sm"]).split(".")[:2])
    # Sail 0.14.1 defaults include H and CFI exception causes independently of
    # whether those extensions are implemented by the selected hart.
    if "H" not in extensions:
        delegatable = base["medeleg"]["delegatable_bits"]
        delegatable["value"] = hex(int(delegatable["value"], 0) & ~((1 << 10) | (0xF << 20)))
    if not extensions.keys() & {"Zicfilp", "Zicfiss"}:
        delegatable = base["medeleg"]["delegatable_bits"]
        delegatable["value"] = hex(int(delegatable["value"], 0) & ~(1 << 18))
    for prefix, field in (("HPM_COUNTER_EN", "writable_hpm_counters"),
                          ("MCOUNTENABLE_EN", "mcounteren_writable_bits"),
                          ("SCOUNTENABLE_EN", "scounteren_writable_bits")):
        base[field] = bits(sum(1 << i for i, value in enumerate(params[prefix]) if value), 32)
    for csr in ("mtvec", "stvec"):
        modes = params[csr.upper() + "_MODES"]
        base[csr]["direct"]["supported"] = 0 in modes
        base[csr]["vectored"]["supported"] = 1 in modes
    base["mtvec"]["direct"]["base_alignment"] = int(math.log2(params["MTVEC_BASE_ALIGNMENT_DIRECT"]))
    for field in ("fs", "vs"):
        values = params[f"MSTATUS_{field.upper()}_LEGAL_VALUES"]
        base["mstatus"][f"{field}_legal_states"] = {
            (0,): "ExtContext_Off", (0, 3): "ExtContext_TwoState",
            (0, 1, 2, 3): "ExtContext_FourState",
        }[tuple(values)]
    trap_fields = {
        "illegal_instruction": "REPORT_ENCODING_IN_MTVAL_ON_ILLEGAL_INSTRUCTION",
        "software_breakpoint": "REPORT_VA_IN_MTVAL_ON_BREAKPOINT",
        "load_address_misaligned": "REPORT_VA_IN_MTVAL_ON_LOAD_MISALIGNED",
        "samo_address_misaligned": "REPORT_VA_IN_MTVAL_ON_STORE_AMO_MISALIGNED",
        "fetch_address_misaligned": "REPORT_VA_IN_MTVAL_ON_INSTRUCTION_MISALIGNED",
    }
    for field, parameter in trap_fields.items():
        base["xtval_nonzero"][field] = params[parameter]
    memory = default["memory"]
    memory["physaddr_bits"] = params["PHYS_ADDR_WIDTH"]
    memory["asidlen"] = params["ASID_WIDTH"]
    if "H" in extensions:
        if any(params[mode + "XLEN"] != [params["MXLEN"]] or params[mode + "_MODE_ENDIANNESS"] != "little" for mode in ("VS", "VU")):
            raise ValueError("Sail guest projection requires native-width little-endian VS mode")
        if not params["VSSTAGE_MODE_BARE"] or not params["GSTAGE_MODE_BARE"]:
            raise ValueError("Sail guest projection requires Bare translation modes")
        memory["vmidlen"] = params["VMID_WIDTH"]
        guest = model_extensions["H"]
        guest["geilen"] = params["NUM_EXTERNAL_GUEST_INTERRUPTS"]
        guest["guest_page_fault_writes_htval"] = params["REPORT_GPA_IN_HTVAL_ON_GUEST_PAGE_FAULT"]
        for mode in guest["vsatp_modes"]:
            guest["vsatp_modes"][mode] = params.get(mode.upper() + "_VSMODE_TRANSLATION", False)
        for mode in guest["hgatp_modes"]:
            guest["hgatp_modes"][mode] = params.get(mode.upper() + "_TRANSLATION", False)
        base["hcounteren_writable_bits"] = bits(sum(1 << i for i, value in enumerate(params["HCOUNTENABLE_EN"]) if value), 32)
        base["vstvec"]["direct"]["supported"] = 0 in params["VSTVEC_MODES"]
        base["vstvec"]["vectored"]["supported"] = 1 in params["VSTVEC_MODES"]
        base["xtval_nonzero"]["virtual_instruction"] = params["REPORT_ENCODING_IN_VSTVAL_ON_VIRTUAL_INSTRUCTION"]
    memory["pmp"]["count"] = pmp_count
    memory["pmp"]["usable_count"] = pmp_usable_count
    memory["pmp"]["grain"] = pmp_granularity - 2
    memory["pmp"]["na4_supported"] = params.get("PMP_NA4_SUPPORTED", pmp_count != 0 and pmp_granularity == 2)
    memory["pmp"]["napot_supported"] = params.get("PMP_NAPOT_SUPPORTED", pmp_count != 0)
    memory["pmp"]["tor_supported"] = params.get("PMP_TOR_SUPPORTED", pmp_count != 0)
    memory["misaligned"]["exceptions"]["load_store"] = {"None": None} if misaligned else {"Some": "AlignmentException"}
    memory["misaligned"]["exceptions"]["vector"] = {"None": None} if misaligned else {"Some": "AlignmentException"}
    memory["misaligned"]["exceptions"]["amo"] = {"Some": "AlignmentException"}
    lrsc_behavior = params["LRSC_MISALIGNED_BEHAVIOR"]
    lrsc_exceptions = {"always raise misaligned exception": "AlignmentException",
                       "always raise access fault": "AccessFault"}
    if lrsc_behavior not in lrsc_exceptions:
        raise ValueError("unsupported LRSC_MISALIGNED_BEHAVIOR")
    memory["misaligned"]["exceptions"]["lrsc"] = {"Some": lrsc_exceptions[lrsc_behavior]}
    ram = next(region for region in memory["regions"] if region["attributes"]["mem_type"] == "MainMemory")
    ram["base"], ram["size"] = bits(origin), bits(size)
    attrs = ram["attributes"]
    attrs.update(atomic_support="AMOArithmetic", misaligned_atomicity_granule_size_exp=0,
                 vector_misaligned_atomicity_granule_size_exp=0, supports_cbo_zero="Zicboz" in extensions)
    for kind in ("load_store", "vector"):
        attrs.setdefault("misaligned_exceptions", {})[kind] = memory["misaligned"]["exceptions"][kind]
    if extensions.keys() & {"Zic64b", "Zicbom", "Zicbop", "Zicboz"}:
        block_size = params["CACHE_BLOCK_SIZE"]
        if type(block_size) is not int or block_size <= 0 or block_size & (block_size - 1):
            raise ValueError("CACHE_BLOCK_SIZE must be a positive power of two")
        if "Zic64b" in extensions and block_size != 64:
            raise ValueError("Zic64b requires CACHE_BLOCK_SIZE=64")
        default["platform"]["cache_block_size_exp"] = block_size.bit_length() - 1
    # ACT requires Sail's CLINT and synthetic interrupt device even for I-only
    # signature builds. Keep their reference-only IO region; these do not claim
    # that the DUT exposes the synthetic devices. Missing DUT hooks fail at runtime.
    io = next(region for region in memory["regions"] if not region["attributes"]["cacheable"])
    io["attributes"]["supports_cbo_zero"] = False
    for kind in ("load_store", "vector"):
        io["attributes"].setdefault("misaligned_exceptions", {})[kind] = {"Some": "AccessFault"}
    memory["regions"] = [io, ram]
    memory["dtb_address"] = bits(origin)
    platform = default["platform"]
    platform["archid"] = params.get("ARCH_ID_VALUE", 0) if params.get("MARCHID_IMPLEMENTED", False) else 0
    platform["impid"] = params.get("IMP_ID_VALUE", 0) if params.get("MIMPID_IMPLEMENTED", False) else 0
    platform["vendorid"] = (params.get("VENDOR_ID_BANK", 0) << 7) | params.get("VENDOR_ID_OFFSET", 0)
    platform["reservation"]["require_exact_reservation_addr"] = params["LRSC_FAIL_ON_NON_EXACT_LRSC"]
    validate_reservation_bounds(platform["reservation"], extensions, params["MXLEN"])
    return default


def test_config(name, compiler, objdump, sail, udb):
    return dict(name=name, compiler_exe=compiler, objdump_exe=objdump,
                ref_model_exe=str(Path(sail).resolve()), udb_config=str(Path(udb).resolve()),
                linker_script="link.ld", dut_include_dir=".", include_priv_tests=True)


def act_udb_configuration(udb, overlay):
    """Apply pinned UDB schema corrections without changing DUT capabilities."""
    if any(entry["name"] in ("H", "Sstvala") for entry in udb["implemented_extensions"]):
        return {**udb, "arch_overlay": str(overlay.resolve())}
    return udb


def main():
    import pyjson5
    from ruamel.yaml import YAML

    parser = argparse.ArgumentParser()
    parser.add_argument("--udb", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--sail", required=True)
    parser.add_argument("--platform", type=Path, required=True)
    args = parser.parse_args()
    for key, value in platform_settings(json.loads(args.platform.read_text()), args.name).items():
        setattr(args, key, value)
    sail = shutil.which(args.sail)
    if not sail:
        parser.error(f"Sail executable not found: {args.sail}; run arch-test-setup")
    version = subprocess.check_output([sail, "--version"], text=True).strip()
    if version != "0.14.1":
        parser.error(f"expected Sail 0.14.1, got {version}")
    udb = YAML(typ="safe").load(args.udb)
    configuration = json.loads(args.udb.with_name("configuration.json").read_text())
    if configuration.get("product") != args.name or YAML(typ="safe").load(configuration["udb"]) != udb:
        parser.error("ACT UDB differs from its resolved product configuration")
    fingerprint = hashlib.sha256(json.dumps(configuration, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()
    model_width = ["--rv32"] if udb["params"]["MXLEN"] == 32 else []
    default = pyjson5.decode(subprocess.check_output([sail, *model_width, "--print-default-config"], text=True))
    config = sail_config(default, udb, args.ram_origin, args.ram_bytes)
    validate_access_fault_region(config, udb["params"], args.access_fault_address, args.access_fault_bytes)
    args.output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parent
    linker = (source / "link.ld.in").read_text()
    for key, value in (("RAM_ORIGIN", args.ram_origin), ("RAM_BYTES", args.ram_bytes), ("TEST_BASE", args.test_base)):
        linker = linker.replace("@" + key + "@", hex(value))
    (args.output / "link.ld").write_text(linker)
    macros = render_rvmodel_macros(
        (source / "rvmodel_macros.h").read_text(), args.access_fault_address
    )
    (args.output / "rvmodel_macros.h").write_text(macros)
    (args.output / "sail.json").write_text(f"// Rhodium configuration SHA-256: {fingerprint}\n" + json.dumps(config, indent=2) + "\n")
    differences = reference_model_differences(udb["params"])
    (args.output / "reference-model-differences.json").write_text(json.dumps(differences, indent=2) + "\n")
    if differences:
        print("ACT reference-model differences (tests remain enabled): " + json.dumps(differences, sort_keys=True))
    subprocess.run([sail, "--config", str(args.output / "sail.json"), "--validate-config"], check=True)
    act_udb = args.output / "act-udb.yaml"
    with act_udb.open("w") as output:
        output.write(f"# Rhodium configuration SHA-256: {fingerprint}\n")
        YAML().dump(act_udb_configuration(udb, source / "udb-overlay"), output)
    act = test_config(args.name, args.compiler, args.objdump, sail, act_udb)
    with (args.output / "test_config.yaml").open("w") as output:
        output.write("# Connects generated UDB and platform files to ACT4.\n")
        YAML().dump(act, output)


if __name__ == "__main__":
    main()
