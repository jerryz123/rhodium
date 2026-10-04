#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import argparse
from pathlib import Path
import resource
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--verilator", default="verilator")
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    sims = Path(__file__).resolve().parents[1]
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    for trace, cosim in ((False, False), (True, False), (False, True), (True, True)):
        name = f"trace{int(trace)}-cosim{int(cosim)}"
        directory = args.build.resolve() / name
        directory.mkdir(parents=True, exist_ok=True)
        flags = "-std=c++20" + (" -DRHEG_TRACE" if trace else "") + (" -DRHODIUM_COSIM" if cosim else "")
        command = [args.verilator, "--binary", "--timing", "--vpi", "--assert", "--build-jobs", "2",
                   "--top-module", "TestDriver", "--Mdir", str(directory), "-CFLAGS", flags,
                   str(sims / "TestDriver.v"), str(sims / "tests/simulation-runtime.sv"),
                   str(sims / "verilator/simulation_runtime.cc"), str(sims / "tests/simulation-runtime.cc")]
        if trace:
            command.append("+define+TEST_TRACE")
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
        (directory / "build.log").write_text(result.stdout)
        if result.returncode:
            raise SystemExit(result.stdout)

        defaults = ["+rheg-trace=test.pftrace"] if trace else []

        def run(extra=(), *, fail=False, cycles=None, options=None):
            result = subprocess.run([str(directory / "VTestDriver"), *(defaults if options is None else options), *extra],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=10)
            if (result.returncode != 0) != fail:
                raise AssertionError(result.stdout)
            if cycles is not None:
                for enabled, message in ((cosim, f"test cosim closed: samples={cycles + 3}"),
                                         (trace, f"test trace closed: cycles={cycles}")):
                    if enabled and result.stdout.count(message) != 1:
                        raise AssertionError(result.stdout)
            return result.stdout

        run(cycles=5)
        run(["+max-cycles=2"], fail=True, cycles=2)
        run(["+runtime-test-target-fail"], fail=True, cycles=5)
        if trace or cosim:
            run(["+runtime-test-fail"], fail=True, cycles=2)
            run(["+runtime-test-finish-fail"], fail=True, cycles=5)
        if trace:
            run(options=[], fail=True)
            run(["+rheg-trace=test.pftrace"], fail=True)
        else:
            run(["+rheg-trace=test.pftrace"], fail=True)
        if cosim:
            output = run(["+cosim-corrupt-order=27"], cycles=5)
            assert "test cosim opened: corruption=27" in output
            run(["+cosim-corrupt-order=invalid"], fail=True)
        else:
            run(["+cosim-corrupt-order=27"], fail=True)
        print(f"{name}: reset/settled scheduling, options, termination, and failure cleanup passed", flush=True)


if __name__ == "__main__":
    main()
