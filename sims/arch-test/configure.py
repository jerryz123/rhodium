#!/usr/bin/env python3
# Prepares all UDB-applicable ACT tests and their Sail/platform configuration.
# SPDX-License-Identifier: Apache-2.0
import argparse
import json
import math
from pathlib import Path
import sys
import shutil
import subprocess


# Script and runpy callers share the simulation-owned projection package.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sail.configuration import bits, encoded_integer, project_architecture, reference_model_differences
from sail.product import fingerprint, model_defaults, product_architecture


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


def sail_config(default, udb, origin, size):
    """Compose the shared hart projection with ACT's synthetic environment."""
    default = project_architecture(default, udb)
    memory = default["memory"]
    extensions = {entry["name"] for entry in udb["implemented_extensions"]}
    ram = next(region for region in memory["regions"] if region["attributes"]["mem_type"] == "MainMemory")
    ram["base"], ram["size"] = bits(origin), bits(size)
    attrs = ram["attributes"]
    attrs.update(atomic_support="AMOArithmetic", misaligned_atomicity_granule_size_exp=0,
                 vector_misaligned_atomicity_granule_size_exp=0, supports_cbo_zero="Zicboz" in extensions)
    for kind in ("load_store", "vector"):
        attrs.setdefault("misaligned_exceptions", {})[kind] = memory["misaligned"]["exceptions"][kind]
    # ACT requires Sail's CLINT and synthetic interrupt device even for I-only
    # signature builds. Keep their reference-only IO region; these do not claim
    # that the DUT exposes the synthetic devices. Missing DUT hooks fail at runtime.
    io = next(region for region in memory["regions"] if not region["attributes"]["cacheable"])
    io["attributes"]["supports_cbo_zero"] = False
    for kind in ("load_store", "vector"):
        io["attributes"].setdefault("misaligned_exceptions", {})[kind] = {"Some": "AccessFault"}
    memory["regions"] = [io, ram]
    memory["dtb_address"] = bits(origin)
    return default


def test_config(name, compiler, objdump, sail, udb):
    return dict(name=name, compiler_exe=compiler, objdump_exe=objdump,
                ref_model_exe=str(Path(sail).resolve()), udb_config=str(Path(udb).resolve()),
                linker_script="link.ld", dut_include_dir=".", include_priv_tests=True)


def act_udb_configuration(udb, overlay):
    """Apply pinned UDB corrections and declare the explicit AMO fault policy."""
    if "AMO_MISALIGNED_BEHAVIOR" in udb["params"] or any(
        entry["name"] in ("H", "Sstvala") for entry in udb["implemented_extensions"]
    ):
        return {**udb, "arch_overlay": str(overlay.resolve())}
    return udb


def main():
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
    udb = YAML(typ="safe").load(args.udb)
    configuration = json.loads(args.udb.with_name("configuration.json").read_text())
    product_architecture(configuration, args.name, udb)
    configuration_fingerprint = fingerprint(configuration)
    default = model_defaults(sail, udb["params"]["MXLEN"])
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
    (args.output / "sail.json").write_text(f"// Rhodium configuration SHA-256: {configuration_fingerprint}\n" + json.dumps(config, indent=2) + "\n")
    differences = reference_model_differences(udb["params"])
    (args.output / "reference-model-differences.json").write_text(json.dumps(differences, indent=2) + "\n")
    if differences:
        print("ACT reference-model differences (tests remain enabled): " + json.dumps(differences, sort_keys=True))
    subprocess.run([sail, "--config", str(args.output / "sail.json"), "--validate-config"], check=True)
    act_udb = args.output / "act-udb.yaml"
    with act_udb.open("w") as output:
        output.write(f"# Rhodium configuration SHA-256: {configuration_fingerprint}\n")
        YAML().dump(act_udb_configuration(udb, source / "udb-overlay"), output)
    act = test_config(args.name, args.compiler, args.objdump, sail, act_udb)
    with (args.output / "test_config.yaml").open("w") as output:
        output.write("# Connects generated UDB and platform files to ACT4.\n")
        YAML().dump(act, output)


if __name__ == "__main__":
    main()
