#!/usr/bin/env python3
# Selects and runs independent behavioral families through direct SV and optional CIRCT comparison.
# SPDX-License-Identifier: Apache-2.0
import argparse
import os
from pathlib import Path
import shutil
import tempfile

from support import ROOT, FIXTURES, WIDTHS, run
from rtl import simulate
from memory import simulate_sync_memory
from assertions import simulate_assertions
from dpi import simulate_dpi


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--differential", action="store_true")
    parser.add_argument("--family", choices=("rtl", "assertions", "sync-memory", "dpi"), action="append",
                        help="run only selected semantic families; defaults to all")
    args = parser.parse_args()
    families = set(args.family or ("rtl", "assertions", "sync-memory", "dpi"))
    verilator = shutil.which("verilator")
    if not verilator:
        parser.error("verilator is required; run make setup-verilator")
    circt = None
    if args.differential:
        circt = os.environ.get("CIRCT_OPT") or shutil.which("circt-opt") or str(ROOT / ".tools/firtool-1.155.0/bin/circt-opt")
        if not Path(circt).is_file():
            parser.error("circt-opt is required for --differential; run make setup-circt or set CIRCT_OPT")
    work = Path(tempfile.mkdtemp(prefix="rhodium-direct-verilog-"))
    try:
        wrapper = ROOT / "tools/run-racket.sh"
        if "dpi" in families:
            dpi_source = run([wrapper, FIXTURES / "emit-direct.rhm", "dpi"], work / "direct-dpi-emit.log")
            dpi_samples = simulate_dpi(work, "direct", dpi_source, True, verilator)
            if args.differential:
                mlir = work / "dpi.mlir"
                mlir.write_text(run([wrapper, FIXTURES / "emit-circt.rhm", "1", "dpi"], work / "circt-dpi-emit.log"))
                reference = run([circt, "--canonicalize", "--cse", "--lower-sim-to-sv",
                                 "--lower-seq-to-sv=disable-mem-randomization=true disable-reg-randomization=true",
                                 "--export-verilog", mlir, "-o", "/dev/null"], work / "circt-dpi-export.log")
                if dpi_samples != simulate_dpi(work, "circt", reference, False, verilator):
                    raise RuntimeError("direct and CIRCT DPI transcripts differ")
        if "sync-memory" in families:
            # Each CIRCT firmem export creates its own helper module inventory.
            # Separate simulations avoid cross-artifact helper-name collisions.
            for width in WIDTHS:
                name = f"direct-sync{width}"
                source = run([wrapper, FIXTURES / "emit-direct.rhm", "sync_memory", str(width)], work / f"{name}-emit.log")
                samples = simulate_sync_memory(work, name, source, width, verilator)
                if args.differential:
                    name = f"circt-sync{width}"
                    mlir = work / f"{name}.mlir"
                    mlir.write_text(run([wrapper, FIXTURES / "emit-circt.rhm", str(width), "sync_memory"], work / f"{name}-emit.log"))
                    reference = run([circt, "--canonicalize", "--cse", "--lower-seq-firmem",
                                     "--lower-seq-to-sv=disable-mem-randomization=true disable-reg-randomization=true",
                                     "--hw-memory-sim=disable-mem-randomization=true disable-reg-randomization=true read-enable-mode=undefined",
                                     "--export-verilog", mlir, "-o", "/dev/null"], work / f"{name}-export.log")
                    if samples != simulate_sync_memory(work, name, reference, width, verilator):
                        raise RuntimeError(f"direct and CIRCT synchronous-memory transcripts differ at width {width}")
        if families & {"rtl", "assertions"}:
            source = run([wrapper, FIXTURES / "emit-direct.rhm"], work / "direct-emit.log")
            direct_samples = simulate(work, "direct", source, True, verilator) if "rtl" in families else None
            if "assertions" in families:
                simulate_assertions(work, "direct", source, True, verilator)
            if args.differential:
                pieces = []
                for width in WIDTHS:
                    for kind in ("scalar", "arithmetic", "hierarchy", "aggregate", "sequential", "dynamic", "write_set", "partial", "memory") + (("types", "assertion", "cdc") if width == 1 else ()):
                        stem = f"{width}-{kind}"
                        mlir = work / f"{stem}.mlir"
                        mlir.write_text(run([wrapper, FIXTURES / "emit-circt.rhm", str(width), kind], work / f"{stem}-emit.log"))
                        pieces.append(run([circt, "--canonicalize", "--cse", "--lower-seq-hlmem", "--lower-verif-to-sv",
                                           "--lower-seq-to-sv=disable-mem-randomization=true disable-reg-randomization=true",
                                           "--sv-mask-non-synthesizable=mode=ifdef macro=SYNTHESIS",
                                           "--export-verilog", mlir, "-o", "/dev/null"], work / f"{stem}-circt.log"))
                circt_source = "\n".join(pieces)
                circt_samples = simulate(work, "circt", circt_source, False, verilator) if "rtl" in families else None
                if "assertions" in families:
                    simulate_assertions(work, "circt", circt_source, False, verilator)
                if direct_samples != circt_samples:
                    raise RuntimeError("direct and CIRCT simulation transcripts differ")
                if "rtl" in families:
                    print("Direct and CIRCT outputs match for every vector", flush=True)
    except BaseException:
        print(f"Validation artifacts retained at {work}", flush=True)
        raise
    else:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
