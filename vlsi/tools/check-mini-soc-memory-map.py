#!/usr/bin/env python3
# Verifies the exact RV64 MiniSoC SRAM mapping shared by synthesis and simulation.

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


MACRO = "sky130_sram_2kbyte_1rw1r_32x512_8"
EXPECTED_SITES = {
    "ram/storage/storage": {
        "decision": "macro",
        "source_suffix": "_4096x128",
        "depth": 4096,
        "width": 128,
        "mask_width": 16,
        "depth_banks": 8,
        "width_slices": 4,
        "instances": 32,
        "bit_utilization": 1.0,
    },
    "rv5stage/l1d/arrays/data_storage/storage": {
        "decision": "macro",
        "source_suffix": "_256x64",
        "depth": 256,
        "width": 64,
        "mask_width": 8,
        "depth_banks": 1,
        "width_slices": 2,
        "instances": 2,
        "bit_utilization": 0.5,
    },
    "rv5stage/l1d/arrays/state_storage/storage": {
        "decision": "infer",
        "source_suffix": "_32x3",
    },
    "rv5stage/l1d/arrays/tag_storage/storage": {
        "decision": "infer",
        "source_suffix": "_32x53",
    },
    "rv5stage/l1i/lines/storage": {
        "decision": "macro",
        "source_suffix": "_256x64",
        "depth": 256,
        "width": 64,
        "mask_width": 8,
        "depth_banks": 1,
        "width_slices": 2,
        "instances": 2,
        "bit_utilization": 0.5,
    },
    "rv5stage/l1i/tags/storage": {
        "decision": "infer",
        "source_suffix": "_32x53",
    },
}
EXPECTED_TOTALS = {
    "memory_sites": 6,
    "mapped_sites": 3,
    "inferred_sites": 3,
    "macro_instances": 36,
}


def fail(message: str) -> None:
    print(f"MiniSoC memory-map check failed: {message}", file=sys.stderr)
    raise SystemExit(2)


def check_mapped_site(path: str, site: dict[str, object], expected: dict[str, object]) -> None:
    if site.get("macro") != MACRO:
        fail(f"{path}: expected macro {MACRO}, got {site.get('macro')}")

    logical = site.get("logical")
    if not isinstance(logical, dict):
        fail(f"{path}: missing logical memory contract")
    expected_logical = {
        "depth": expected["depth"],
        "width": expected["width"],
        "mask_granularity": 8,
        "mask_width": expected["mask_width"],
        "read_ports": 0,
        "write_ports": 0,
        "read_write_ports": 1,
        "read_latency": 1,
        "write_latency": 1,
    }
    for name, value in expected_logical.items():
        if logical.get(name) != value:
            fail(f"{path}: expected logical {name}={value}, got {logical.get(name)}")

    mapping = site.get("mapping")
    if not isinstance(mapping, dict):
        fail(f"{path}: missing macro mapping")
    expected_mapping = {
        "macro": MACRO,
        "interface": "openram_1rw1r",
        "depth_banks": expected["depth_banks"],
        "width_slices": expected["width_slices"],
        "instances_per_site": expected["instances"],
    }
    for name, value in expected_mapping.items():
        if mapping.get(name) != value:
            fail(f"{path}: expected mapping {name}={value}, got {mapping.get(name)}")
    if mapping.get("bit_utilization") != expected["bit_utilization"]:
        fail(
            f"{path}: expected bit utilization {expected['bit_utilization']}, "
            f"got {mapping.get('bit_utilization')}"
        )

    instances = mapping.get("instances")
    if not isinstance(instances, list) or len(instances) != expected["instances"]:
        fail(f"{path}: expected {expected['instances']} macro instances")
    expected_coordinates = {
        (depth_bank, width_slice)
        for depth_bank in range(int(expected["depth_banks"]))
        for width_slice in range(int(expected["width_slices"]))
    }
    actual_coordinates = {
        (instance.get("depth_bank"), instance.get("width_slice"))
        for instance in instances
        if isinstance(instance, dict)
    }
    if actual_coordinates != expected_coordinates:
        fail(f"{path}: macro bank/slice coordinates do not cover the expected tiling")

    model = site.get("functional_verilog")
    if not isinstance(model, str) or not Path(model).is_file():
        fail(f"{path}: no usable functional model")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--top", required=True)
    parser.add_argument("--scope-prefix", default="")
    arguments = parser.parse_args()

    try:
        manifest = json.loads(arguments.manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(str(error))
    if manifest.get("schema_version") != 2:
        fail("expected a site-aware schema_version 2 manifest")

    selection = manifest.get("selection_policy")
    if not isinstance(selection, dict):
        fail("missing selection policy")
    expected_selection = {
        "default": "infer",
        "top": arguments.top,
        "policy_top": "MiniSoC",
        "scope_prefix": arguments.scope_prefix,
    }
    for name, value in expected_selection.items():
        if selection.get(name) != value:
            fail(f"expected selection {name}={value}, got {selection.get(name)}")

    sites = manifest.get("sites")
    if not isinstance(sites, list):
        fail("missing site list")
    sites_by_path = {
        site.get("path"): site
        for site in sites
        if isinstance(site, dict) and isinstance(site.get("path"), str)
    }
    if len(sites_by_path) != len(sites):
        fail("site paths must be present and unique")
    if set(sites_by_path) != set(EXPECTED_SITES):
        missing = sorted(set(EXPECTED_SITES) - set(sites_by_path))
        extra = sorted(set(sites_by_path) - set(EXPECTED_SITES))
        fail(f"unexpected site inventory; missing={missing}, extra={extra}")

    for path, expected in EXPECTED_SITES.items():
        site = sites_by_path[path]
        if site.get("decision") != expected["decision"]:
            fail(f"{path}: expected decision {expected['decision']}, got {site.get('decision')}")
        expected_instance_path = "/".join(
            part for part in (arguments.scope_prefix, path) if part
        )
        if site.get("instance_path") != expected_instance_path:
            fail(
                f"{path}: expected instance path {expected_instance_path}, "
                f"got {site.get('instance_path')}"
            )
        source_module = site.get("source_module")
        if not isinstance(source_module, str) or not source_module.endswith(
            str(expected["source_suffix"])
        ):
            fail(f"{path}: source module does not have the expected RV64 shape")
        if expected["decision"] == "macro":
            check_mapped_site(path, site, expected)
        elif "mapping" in site or "macro" in site:
            fail(f"{path}: inferred metadata unexpectedly carries a macro mapping")

    totals = manifest.get("totals")
    if not isinstance(totals, dict):
        fail("missing mapping totals")
    for name, value in EXPECTED_TOTALS.items():
        if totals.get(name) != value:
            fail(f"expected totals {name}={value}, got {totals.get(name)}")

    print(
        "validated RV64 MiniSoC mapping: "
        "3 of 6 sites use 36 Sky130 SRAM macros; each 2 KiB L1 uses half-depth"
    )


if __name__ == "__main__":
    main()
