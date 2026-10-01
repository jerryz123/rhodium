# Loads identity-bound product architecture and defaults from the pinned Sail executable.
# SPDX-License-Identifier: Apache-2.0
import hashlib
import json
import subprocess


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()


def product_architecture(configuration, name, udb=None):
    """Use the resolved product's UDB, optionally checking an accompanying export."""
    from ruamel.yaml import YAML
    resolved = YAML(typ="safe").load(configuration["udb"])
    if configuration.get("schema") != 1 or configuration.get("product") != name:
        raise ValueError("configuration must identify the selected product")
    if configuration["xlen"] != resolved["params"]["MXLEN"] or (udb is not None and resolved != udb):
        raise ValueError("UDB differs from its resolved product configuration")
    return resolved


def model_defaults(sail, xlen):
    """Reject version drift before loading the matching XLEN model defaults."""
    import pyjson5
    version = subprocess.check_output([str(sail), "--version"], text=True).strip()
    if version != "0.14.1":
        raise ValueError(f"expected Sail 0.14.1, got {version}")
    width = ["--rv32"] if xlen == 32 else []
    return pyjson5.decode(subprocess.check_output([str(sail), *width, "--print-default-config"], text=True))
