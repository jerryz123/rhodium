#!/usr/bin/env python3
# Projects an identity-bound Mini/Simple environment onto the shared Sail architecture.
# SPDX-License-Identifier: Apache-2.0
import argparse
import copy
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sail.configuration import bits, project_architecture, reference_model_differences
from sail.product import fingerprint, model_defaults, product_architecture


def checked_product(exported, name):
    """Keep architecture and physical environment bound to their resolved export."""
    configuration, environment = exported["configuration"], exported["environment"]
    for key in ("configuration", "environment"):
        if fingerprint(exported[key]) != exported[key + "_fingerprint"]:
            raise ValueError(f"{key} fingerprint mismatch")
    udb = product_architecture(configuration, name)
    if environment["schema"] != 1:
        raise ValueError("unsupported co-simulation environment schema")
    return udb, environment


def project_environment(config, environment):
    """Replace all reference devices/PMAs; private backing never implies device replay."""
    config = copy.deepcopy(config)
    regions = []
    limit = 1 << config["memory"]["physaddr_bits"]
    previous_end = 0
    source_regions = sorted(environment["regions"], key=lambda region: region["range"]["address"])
    for source in source_regions:
        address, size = source["range"]["address"], source["range"]["size"]
        if (type(address) is not int or type(size) is not int or size <= 0 or
                address < previous_end or address + size > limit):
            raise ValueError("Sail PMAs require nonoverlapping, addressable regions")
        previous_end = address + size
        pma = source["attributes"]
        if (address % 4096 or size % 4096) and (not pma["device"] or pma["executable"] or pma["cacheable"] or pma["atomic"] or pma["cache_block_zero"]):
            raise ValueError("only unsplittable non-executable device PMAs may be smaller than a page")
        # Split accesses require cacheable main memory. Page-table requests use
        # the same physical read/write permissions as ordinary routed requests.
        main = pma["cacheable"] and not pma["device"]
        attrs = {key: pma[key] for key in ("readable", "writable", "executable", "cacheable", "read_idempotent")}
        attrs.update(
            mem_type="MainMemory" if main else "IOMemory",
            coherent=main, write_idempotent=not pma["device"],
            atomic_support="AMOArithmetic" if pma["atomic"] else "AMONone",
            reservability="RsrvEventual" if pma["atomic"] else "RsrvNone",
            supports_pte_read=pma["readable"],
            supports_pte_write=pma["writable"],
            supports_cbo_zero=pma["cache_block_zero"],
            misaligned_atomicity_granule_size_exp=0,
            vector_misaligned_atomicity_granule_size_exp=0,
            misaligned_exceptions={
                "load_store": {"None": None} if main else {"Some": "AccessFault"},
                "vector": {"None": None} if main else {"Some": "AccessFault"},
                "amo": "AccessFault", "lrsc": "AccessFault",
            },
        )
        regions.append({"base": bits(address), "size": bits(size), "attributes": attrs,
                        "include_in_device_tree": False})
    if not regions:
        raise ValueError("co-simulation requires a physical map")
    backing = sorted(environment["private_memory"], key=lambda region: region["address"])
    end = 0
    for region in backing:
        address, size = region["address"], region["size"]
        if type(address) is not int or type(size) is not int or size <= 0 or address < end:
            raise ValueError("private memory ranges must be nonempty and nonoverlapping")
        if not any(source["range"] == region and not source["attributes"]["device"] for source in source_regions):
            raise ValueError("private memory must match a non-device PMA region")
        end = address + size
    reset = environment["reset_pc"]
    if not any(r["address"] <= reset < r["address"] + r["size"] for r in backing):
        raise ValueError("reset PC must lie in private backing")
    if not any(s["range"]["address"] <= reset < s["range"]["address"] + s["range"]["size"]
               and s["attributes"]["executable"] for s in source_regions):
        raise ValueError("reset PC must be executable")
    config["memory"]["regions"] = regions
    # No reference-generated DTB: the real BootROM image will be mirrored by the loader.
    config["memory"]["dtb_address"] = bits(0)
    platform = config["platform"]
    platform["hartid"] = environment["hart_id"]
    platform["clock_frequency"] = environment["clock_frequency_hz"]
    platform["clint"]["supported"] = False
    platform["simple_interrupt_generator"]["supported"] = False
    return config


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--product", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--sail", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    exported = json.loads(args.product.read_text())
    udb, environment = checked_product(exported, args.name)
    config = project_environment(project_architecture(model_defaults(args.sail, udb["params"]["MXLEN"]), udb), environment)
    args.output.mkdir(parents=True, exist_ok=True)
    config_path = args.output / "sail.json"
    config_path.write_text(json.dumps(config, indent=2) + "\n")
    # Full Sail validation happens inside SailReference, with host time enabled.
    # The standalone executable has no host provider and requires its own CLINT.
    differences = reference_model_differences(udb["params"])
    manifest = {
        "schema": 1, "product": args.name,
        "configuration_fingerprint": exported["configuration_fingerprint"],
        "environment_fingerprint": exported["environment_fingerprint"],
        "sail_configuration_fingerprint": fingerprint(config),
        "reset_pc": environment["reset_pc"], "private_memory": environment["private_memory"],
        "timebase_frequency_hz": environment["timebase_frequency_hz"],
        "reference_model_differences": differences,
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (args.output / "reference-model-differences.json").write_text(json.dumps(differences, indent=2) + "\n")
    if differences:
        print("Co-simulation reference-model differences: " + json.dumps(differences, sort_keys=True))


if __name__ == "__main__":
    main()
