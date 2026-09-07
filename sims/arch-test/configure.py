#!/usr/bin/env python3
# Prepares an ACT integer-test bundle from generated UDB and platform memory inputs.
import argparse
import json
import math
from pathlib import Path
import shutil
import subprocess


def bits(value, width=64):
    return {"len": width, "value": hex(value)}


def sail_config(default, udb, origin, size):
    """Project the settings needed by unprivileged I tests and their M-mode startup.

    This is deliberately not a general UDB-to-Sail converter. Privileged tests
    remain disabled until WARL, delegation, interrupts, and PMAs are modeled.
    """
    params = udb["params"]
    extensions = {entry["name"]: str(entry["version"]).removeprefix("= ") for entry in udb["implemented_extensions"]}
    if params["MXLEN"] != 64 or params["NUM_PMP_ENTRIES"] != 0:
        raise ValueError("initial ACT adapter requires RV64 with no PMP")
    if params["MISALIGNED_LDST"] or params["MISALIGNED_LDST_EXCEPTION_PRIORITY"] != "high":
        raise ValueError("initial ACT adapter requires high-priority misaligned load/store traps")
    if params["M_MODE_ENDIANNESS"] != "little":
        raise ValueError("initial ACT adapter requires little-endian M mode")
    model_extensions = default["extensions"]
    if extensions.keys() & {"V", "Stateen", "Smstateen", "Ssstateen"}:
        raise ValueError("vector and state-enable configurations need an expanded Sail projection")
    unknown = extensions.keys() - model_extensions.keys() - {"I", "C", "Sm"}
    if unknown:
        raise ValueError(f"extensions need Sail mapping: {sorted(unknown)}")
    for name, options in model_extensions.items():
        if "supported" in options:
            options["supported"] = name in extensions
    model_extensions["V"]["support_level"] = "Disabled"
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
                 vector_misaligned_atomicity_granule_size_exp=0, supports_cbo_zero=False)
    # ACT requires Sail's CLINT and synthetic interrupt device even for I-only
    # signature builds. Keep their reference-only IO region; these do not claim
    # that the DUT exposes the synthetic devices or enable privileged tests.
    io = next(region for region in memory["regions"] if not region["attributes"]["cacheable"])
    memory["regions"] = [io, ram]
    memory["dtb_address"] = bits(origin)
    default["platform"]["reservation"]["require_exact_reservation_addr"] = params["LRSC_FAIL_ON_NON_EXACT_LRSC"]
    return default


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
    (args.output / "sail.json").write_text("// Configures Sail for the selected UDB integer-test target.\n" + json.dumps(config, indent=2) + "\n")
    subprocess.run([sail, "--config", str(args.output / "sail.json"), "--validate-config"], check=True)
    act = dict(name=args.name, compiler_exe=args.compiler, objdump_exe=args.objdump,
               ref_model_exe=str(Path(sail).resolve()), udb_config=str(args.udb.resolve()),
               linker_script="link.ld", dut_include_dir=".", include_priv_tests=False)
    with (args.output / "test_config.yaml").open("w") as output:
        output.write("# Connects generated UDB and platform files to ACT4.\n")
        YAML().dump(act, output)


if __name__ == "__main__":
    main()
