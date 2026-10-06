# Loads identity-bound config architecture and defaults from the pinned Sail executable.
# SPDX-License-Identifier: Apache-2.0
import hashlib
import json
import subprocess


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()


def config_architecture(configuration, name, udb=None):
    """Use the resolved config's UDB, optionally checking an accompanying export."""
    from ruamel.yaml import YAML
    resolved = YAML(typ="safe").load(configuration["udb"])
    if configuration.get("schema") != 1 or configuration.get("config") != name:
        raise ValueError("configuration must identify the selected config")
    if configuration["xlen"] != resolved["params"]["MXLEN"] or (udb is not None and resolved != udb):
        raise ValueError("UDB differs from its resolved config")
    return resolved


def model_defaults(sail, xlen):
    """Check release and required schema controls before loading XLEN defaults."""
    import pyjson5
    from .configuration import TRANSFORMED_INSTRUCTION_PARAMETERS
    version = subprocess.check_output([str(sail), "--version"], text=True).strip()
    if version != "0.14.1":
        raise ValueError(f"expected Sail 0.14.1, got {version}")
    width = ["--rv32"] if xlen == 32 else []
    default = pyjson5.decode(subprocess.check_output([str(sail), *width, "--print-default-config"], text=True))
    # Master retains the 0.14.1 release string; an older release lacks these controls.
    inhibit = default.get("base", {}).get("mcountinhibit", {})
    transformed = default.get("extensions", {}).get("H", {}).get("transformed_instruction", {})
    if (not {"supported", "writable_bits"} <= inhibit.keys()
            or not TRANSFORMED_INSTRUCTION_PARAMETERS.keys() <= transformed.keys()
            or type(default.get("base", {}).get("tselect_present")) is not bool):
        raise ValueError("Sail lacks pinned mcountinhibit/transformed-instruction/tselect controls; rebuild with arch-test-sail-setup")
    return default
