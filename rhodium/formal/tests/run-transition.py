#!/usr/bin/env python3
# Replays register traces against native/HDL models and independent oracles.
# SPDX-License-Identifier: Apache-2.0
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent


def run(command, work, label):
    result = subprocess.run([str(item) for item in command], cwd=ROOT,
                            env=dict(os.environ, PLTCOLLECTS=str(ROOT) + os.pathsep),
                            capture_output=True, text=True, timeout=600)
    (work / f"{label}.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"{label} failed; see {work / (label + '.log')}")
    return result.stdout


def build_model(folder, target, top):
    bench = folder / "driver.cpp"
    if target == "rsim":
        exe = folder / "sim"
        run([*shlex.split(os.environ.get("CXX", "c++")), "-std=c++20", "-O1", "-g",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I", folder,
             folder / f"{top}.cpp", bench, "-o", exe], folder, "build")
    else:
        source = folder / f"{top}.sv"
        if target == "circt":
            circt = os.environ.get("CIRCT_OPT") or str(ROOT / ".tools/firtool-1.155.0/bin/circt-opt")
            source.write_text(run([circt, "--canonicalize", "--cse", "--lower-verif-to-sv",
                                   "--lower-seq-to-sv=disable-mem-randomization=true disable-reg-randomization=true",
                                   "--export-verilog", folder / f"{top}.mlir", "-o", "/dev/null"], folder, "lower"))
        build = folder / "obj"
        run(["verilator", "--cc", "--exe", "--build", "--assert", "--top-module", top,
             "--Mdir", build, source, bench], folder, "build")
        exe = build / f"V{top}"
    return exe


def oracle():
    count = first = second = packed = 0
    rows = []
    for cycle in range(40):
        data = cycle * 3 % 8
        if cycle >= 2:
            rows.append(f"{cycle} {count} {first} {second} {packed}")
        count = 5 if cycle in (0, 1, 12, 31) else (count + 1) % 8
        first, second = data, first
        packed = data % 4 + (packed % 4) * 4
    return "\n".join(rows) + "\n"


def driver(native):
    header = "Transition.hpp" if native else "VTransition.h"
    model = "rsim_pTransition::Model" if native else "VTransition"
    inp = "dut.inputs.p" if native else "dut."
    out = "dut.outputs().p" if native else "dut."
    names = [name.replace("_", "_u") if native else name
             for name in ("count_out", "p1_out", "p2_out", "packed_out")]
    print_values = " << ' ' << ".join(f"static_cast<unsigned>({out}{name})" for name in names)
    edge = "dut.tick();" if native else "dut.clock = 1; dut.eval(); dut.clock = 0; dut.eval();"
    return f'''// Observes pre-edge state only after reset/data have defined every printed field.
// SPDX-License-Identifier: Apache-2.0
#include "{header}"
#include <iostream>
int main() {{
  {model} dut;
  for (unsigned cycle = 0; cycle < 40; ++cycle) {{
    {inp}reset = cycle == 0 || cycle == 1 || cycle == 12 || cycle == 31;
    {inp}data = cycle * 3 % 8;
    dut.eval();
    if (cycle >= 2) std::cout << cycle << ' ' << {print_values} << '\\n';
    {edge}
  }}
}}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--circt", action="store_true")
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix="rhodium-transition-replay-"))
    try:
        targets = ("rsim", "verilog") + (("circt",) if args.circt else ())
        for target in targets:
            folder = work / target
            folder.mkdir()
            run([ROOT / "tools/run-racket.sh", HERE / "emit-transition.rhm", folder, target], folder, "emit")
            expected = (folder / "expected.txt").read_text()
            if expected != oracle():
                raise AssertionError("Rosette transition trace differs from independent recurrence")
            native = target == "rsim"
            bench = folder / "driver.cpp"
            bench.write_text(driver(native))
            exe = build_model(folder, target, "Transition")
            if run([exe], folder, "simulate") != expected:
                raise AssertionError(f"{target} disagrees with pre-edge transition trace")
        print("Register transitions: 38 defined samples agree across Rosette, Python, native, and HDL")
    except BaseException:
        print(f"Validation artifacts retained at {work}")
        raise
    else:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
