#!/usr/bin/env python3
# Runs existing authored-circuit benches through direct SV and optional CIRCT targets.
# SPDX-License-Identifier: Apache-2.0
import argparse
import os
from pathlib import Path
import shutil
import tempfile

from support import ROOT, FIXTURES, run

# Reuse the owning packages' benches and native model; no generated copies or
# alternative circuit implementations belong to this compiler integration test.
CASES = {
    "sync-ram": ("sync_ram_tb", "rhodium/std/tests/circt/verilog/sync-ram_tb.sv",
                 (), "fixed-latency masked SyncRam passed"),
    "uart-dpi": ("uart_dpi_tb", "devices/tests/circt/verilog/uart-dpi_tb.sv",
                 ("devices/tests/circt/verilog/uart-dpi_dpi.cpp", "devices/uart/dpi/uart_dpi.cc"),
                 "UART DPI PTY and serial behavior passed"),
}


def simulate(work, fixture, route, source, verilator):
    """The same bench assertions and native sources are the behavioral oracle."""
    top, bench, native, success = CASES[fixture]
    name = f"{fixture}-{route}"
    sv = work / f"{name}.sv"
    sv.write_text(source)
    build = work / f"{name}-build"
    # The minimum UART divider retains an unsigned comparison against zero in
    # unoptimized direct RTL. It is valid constant-false logic; keep other lint
    # diagnostics fatal and all runtime assertions enabled on both routes.
    run([verilator, "--binary", "--timing", "--assert", "-Wno-UNSIGNED", "--build-jobs", "2",
         "--top-module", top, "--Mdir", build, "-o", "sim", sv, ROOT / bench,
         *(ROOT / path for path in native)], work / f"{name}-build.log")
    output = run([build / "sim"], work / f"{name}-run.log", timeout=60)
    if output.splitlines().count(success) != 1:
        raise RuntimeError(f"missing completion marker; see {name}-run.log")
    print(f"{fixture} / {route}: {success}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--differential", action="store_true")
    parser.add_argument("--fixture", choices=CASES, action="append")
    args = parser.parse_args()
    verilator = shutil.which("verilator")
    if not verilator:
        parser.error("verilator is required; run make setup-verilator")
    circt = None
    if args.differential:
        circt = os.environ.get("CIRCT_OPT") or shutil.which("circt-opt") or str(ROOT / ".tools/firtool-1.155.0/bin/circt-opt")
        if not Path(circt).is_file():
            parser.error("circt-opt is required for --differential; set CIRCT_OPT or run make setup-circt")
    work = Path(tempfile.mkdtemp(prefix="rhodium-backend-integration-"))
    try:
        for fixture in args.fixture or CASES:
            emitter = [ROOT / "tools/run-racket.sh", FIXTURES / "emit-integration.rhm"]
            direct = run([*emitter, "direct", fixture], work / f"{fixture}-direct-emit.log")
            simulate(work, fixture, "direct", direct, verilator)
            if args.differential:
                mlir = work / f"{fixture}.mlir"
                # The emitter compares both targets' manifests on the same
                # elaboration before publishing any reference artifact.
                mlir.write_text(run([*emitter, "circt", fixture], work / f"{fixture}-circt-emit.log"))
                reference = run([circt, "--canonicalize", "--cse", "--lower-seq-hlmem", "--lower-seq-firmem",
                                 "--lower-sim-to-sv", "--lower-verif-to-sv",
                                 "--lower-seq-to-sv=disable-mem-randomization=true disable-reg-randomization=true",
                                 "--hw-memory-sim=disable-mem-randomization=true disable-reg-randomization=true read-enable-mode=undefined",
                                 "--export-verilog", mlir, "-o", "/dev/null"], work / f"{fixture}-export.log")
                simulate(work, fixture, "circt", reference, verilator)
                print(f"{fixture}: compatible manifests and both routes passed the shared behavioral oracle", flush=True)
    except BaseException:
        print(f"Validation artifacts retained at {work}", flush=True)
        raise
    else:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
