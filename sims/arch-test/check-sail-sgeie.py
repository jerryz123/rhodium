#!/usr/bin/env python3
# Checks the patched Sail model's GEILEN-dependent mie/hie SGEIE alias behavior.
# SPDX-License-Identifier: Apache-2.0
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

import pyjson5


CSR_READ = re.compile(r"^CSR \S+ \((0x[0-9a-f]+)\) -> (0x[0-9a-f]+)$", re.MULTILINE | re.IGNORECASE)
EXPECTED_CSR_ORDER = (0x303, 0x304, 0x604, 0x304, 0x304, 0x604)
SGEIE = 1 << 12


def check_model(sail, compiler, config_path):
    config = pyjson5.decode(config_path.read_text())
    hypervisor = config["extensions"]["H"]
    if not hypervisor["supported"] or hypervisor["geilen"] != 0:
        raise ValueError("CSR probe requires an H-enabled GEILEN=0 platform configuration")
    if config["base"]["xlen"] != 64:
        raise ValueError("CSR probe requires an RV64 platform configuration")
    ram = next(region for region in config["memory"]["regions"]
               if region["attributes"]["executable"] and region["attributes"]["readable"])
    ram_origin = int(ram["base"]["value"], 0)

    source = Path(__file__).with_name("sgeie-probe.S")
    with tempfile.TemporaryDirectory(prefix="rhodium-sgeie-") as directory:
        directory = Path(directory)
        elf = directory / "sgeie-probe.elf"
        subprocess.run([
            compiler, "-march=rv64im_zicsr", "-mabi=lp64", "-nostdlib", "-nostartfiles",
            f"-Wl,-Ttext-segment={hex(ram_origin)}", "-Wl,-e,_start", str(source), "-o", str(elf),
        ], check=True)
        for geilen in (0, 1):
            hypervisor["geilen"] = geilen
            model_config = directory / f"geilen-{geilen}.json"
            model_config.write_text(json.dumps(config))
            run = subprocess.run([
                sail, "--config", str(model_config), "--trace-csr", "--inst-limit", "12", str(elf),
            ], text=True, capture_output=True)
            if run.returncode:
                raise AssertionError(f"GEILEN={geilen}: Sail failed:\n{run.stdout}\n{run.stderr}")
            trace = run.stdout
            reads = [(int(csr, 16), int(value, 16)) for csr, value in CSR_READ.findall(trace)
                     if int(csr, 16) in EXPECTED_CSR_ORDER]
            if tuple(csr for csr, _value in reads) != EXPECTED_CSR_ORDER:
                raise AssertionError(f"GEILEN={geilen}: unexpected CSR trace: {trace}")
            expected_bits = (geilen, geilen, geilen, 0, geilen, geilen)
            actual_bits = tuple(bool(value & SGEIE) for _csr, value in reads)
            if actual_bits != tuple(bool(bit) for bit in expected_bits):
                raise AssertionError(
                    f"GEILEN={geilen}: SGEIE reads {actual_bits}, expected {expected_bits}\n{trace}"
                )
            print(f"GEILEN={geilen}: mideleg/mie/hie SGEIE alias passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sail", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args()
    check_model(args.sail, args.compiler, args.config)


if __name__ == "__main__":
    main()
