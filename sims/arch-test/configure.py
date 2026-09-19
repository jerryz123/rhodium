#!/usr/bin/env python3
# Prepares all UDB-applicable ACT tests and their Sail/platform configuration.
# SPDX-License-Identifier: Apache-2.0
import argparse
import json
import math
from pathlib import Path
import re
import shutil
import subprocess


def bits(value, width=64):
    return {"len": width, "value": hex(value)}


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


def validate_reservation_bounds(reservation, extensions):
    # Sail 0.14 has naturally aligned, fixed-size reservation sets, not switches
    # for these guarantees. Retain its chosen size; the extensions are bounds,
    # not requests to enlarge reservations to a cache line.
    for name, maximum_exp in RESERVATION_BOUNDS.items():
        if name not in extensions:
            continue
        if extensions[name] != "1.0.0":
            raise ValueError(f"{name} needs a Sail mapping for version {extensions[name]}")
        size_exp = reservation["reservation_set_size_exp"]
        if type(size_exp) is not int or not 3 <= size_exp <= maximum_exp:
            raise ValueError(f"{name} requires an RV64 Sail reservation size between 8 and {1 << maximum_exp} bytes")


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
    vector["max_index_eew_exp"] = power_of_two_exp("VECTOR_LS_INDEX_MAX_EEW", max_index_eew)
    vector["vl_use_ceil"] = False
    vector["reserved_behavior"]["illegal_vtype"] = "IllegalVtype_SetVill" if vill else "IllegalVtype_Illegal"
    vector["reserved_behavior"]["vstart_out_of_bounds"] = "Vstart_Ignore"
    vector["vstart"]["zero_required"].update(arith=False, scalar_move=False)
    return selected | zvl


def sail_config(default, udb, origin, size):
    """Project modeled UDB settings; surface remaining model/platform gaps in ACT."""
    params = udb["params"]
    extensions = {entry["name"]: str(entry["version"]).removeprefix("= ") for entry in udb["implemented_extensions"]}
    if params["MXLEN"] != 64 or params["NUM_PMP_ENTRIES"] != 0:
        raise ValueError("initial ACT adapter requires RV64 with no PMP")
    if params["MISALIGNED_LDST"] or params["MISALIGNED_LDST_EXCEPTION_PRIORITY"] != "high":
        raise ValueError("initial ACT adapter requires high-priority misaligned load/store traps")
    if params["M_MODE_ENDIANNESS"] != "little":
        raise ValueError("initial ACT adapter requires little-endian M mode")
    model_extensions = default["extensions"]
    if extensions.keys() & {"Stateen", "Smstateen", "Ssstateen"}:
        raise ValueError("state-enable configurations need an expanded Sail projection")
    vector_extensions = project_vector(model_extensions, extensions, params)
    unknown = extensions.keys() - model_extensions.keys() - {"I", "C", "Sm"} - RESERVATION_BOUNDS.keys() - vector_extensions
    if unknown:
        raise ValueError(f"extensions need Sail mapping: {sorted(unknown)}")
    for name, options in model_extensions.items():
        if "supported" in options:
            options["supported"] = name in extensions
    for name in ("Smstateen", "Ssstateen"):
        model_extensions["Stateen"][name]["supported"] = False
    base = default["base"]
    base["xlen"] = params["MXLEN"]
    base["E"] = False
    base["writable_misa"] = any(value for key, value in params.items() if key.startswith("MUTABLE_MISA_"))
    base["privileged_isa_version"] = "Privileged_ISA_" + "_".join(str(extensions["Sm"]).split(".")[:2])
    # Sail 0.14 defaults include H; its exception codes are reserved without H.
    if "H" not in extensions:
        delegatable = base["medeleg"]["delegatable_bits"]
        delegatable["value"] = hex(int(delegatable["value"], 0) & ~((1 << 10) | (0xF << 20)))
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
    memory["pmp"]["count"] = memory["pmp"]["usable_count"] = 0
    memory["misaligned"]["exceptions"]["load_store"] = {"Some": "AlignmentException"}
    memory["misaligned"]["exceptions"]["amo"] = {"Some": "AlignmentException"}
    memory["misaligned"]["exceptions"]["lrsc"] = {"Some": "AlignmentException"}
    ram = next(region for region in memory["regions"] if region["attributes"]["mem_type"] == "MainMemory")
    ram["base"], ram["size"] = bits(origin), bits(size)
    attrs = ram["attributes"]
    attrs.update(atomic_support="AMOArithmetic", misaligned_atomicity_granule_size_exp=0,
                 vector_misaligned_atomicity_granule_size_exp=0, supports_cbo_zero="Zicboz" in extensions)
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
    memory["regions"] = [io, ram]
    memory["dtb_address"] = bits(origin)
    default["platform"]["reservation"]["require_exact_reservation_addr"] = params["LRSC_FAIL_ON_NON_EXACT_LRSC"]
    validate_reservation_bounds(default["platform"]["reservation"], extensions)
    return default


def test_config(name, compiler, objdump, sail, udb):
    return dict(name=name, compiler_exe=compiler, objdump_exe=objdump,
                ref_model_exe=str(Path(sail).resolve()), udb_config=str(Path(udb).resolve()),
                linker_script="link.ld", dut_include_dir=".", include_priv_tests=True)


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
    for name in ("ram-origin", "ram-bytes", "test-base"):
        parser.add_argument("--" + name, type=lambda value: int(value, 0), required=True)
    args = parser.parse_args()
    if args.ram_bytes <= 0 or not args.ram_origin <= args.test_base < args.ram_origin + args.ram_bytes:
        parser.error("test entry must lie in a nonempty RAM window")
    if args.ram_origin % 4096 or args.ram_bytes % 4096:
        parser.error("Sail RAM regions must be page aligned")
    sail = shutil.which(args.sail)
    if not sail:
        parser.error(f"Sail executable not found: {args.sail}; run arch-test-setup")
    version = subprocess.check_output([sail, "--version"], text=True).strip()
    if version != "0.14":
        parser.error(f"expected Sail 0.14, got {version}")
    default = pyjson5.decode(subprocess.check_output([sail, "--print-default-config"], text=True))
    udb = YAML(typ="safe").load(args.udb)
    config = sail_config(default, udb, args.ram_origin, args.ram_bytes)
    args.output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parent
    linker = (source / "link.ld.in").read_text()
    for key, value in (("RAM_ORIGIN", args.ram_origin), ("RAM_BYTES", args.ram_bytes), ("TEST_BASE", args.test_base)):
        linker = linker.replace("@" + key + "@", hex(value))
    (args.output / "link.ld").write_text(linker)
    shutil.copyfile(source / "rvmodel_macros.h", args.output / "rvmodel_macros.h")
    (args.output / "sail.json").write_text("// Configures Sail for the selected UDB target.\n" + json.dumps(config, indent=2) + "\n")
    subprocess.run([sail, "--config", str(args.output / "sail.json"), "--validate-config"], check=True)
    act = test_config(args.name, args.compiler, args.objdump, sail, args.udb)
    with (args.output / "test_config.yaml").open("w") as output:
        output.write("# Connects generated UDB and platform files to ACT4.\n")
        YAML().dump(act, output)


if __name__ == "__main__":
    main()
