#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import argparse
from pathlib import Path
import resource
import subprocess
import sys
import shlex

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.build.paths import prefix_flags


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--verilator", default="verilator")
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    sims = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(sims / 'program-test'))
    from performance import measure
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    for trace, cosim, stats in ((False, False, False), (True, False, False), (False, True, False),
                                (True, True, False), (False, False, True), (True, True, True)):
        name = f"trace{int(trace)}-cosim{int(cosim)}-stats{int(stats)}"
        directory = args.build.resolve() / name
        directory.mkdir(parents=True, exist_ok=True)
        flags = shlex.join(prefix_flags(directory)) + " -std=c++20" + (" -DRHEG_TRACE" if trace else "") + (" -DRHODIUM_COSIM" if cosim else "")
        if stats:
            flags += f" -DRHODIUM_STATS -I{sims / 'tests'}"
        command = [args.verilator, "--binary", "--timing", "--vpi", "--assert", "--build-jobs", "2",
                   "--top-module", "TestDriver", "--Mdir", str(directory), "-CFLAGS", flags,
                   str(sims / "TestDriver.v"), str(sims / "tests/simulation-runtime.sv"),
                   str(sims / "verilator/simulation_runtime.cc"), str(sims / "tests/simulation-runtime.cc"),
                   str(sims.parent / "devices/uart/dpi/uart_dpi.cc")]
        if trace:
            command.append("+define+TEST_TRACE")
        if stats:
            command.extend(["+define+TEST_STATS", str(sims / 'statistics/collector.cc')])
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
            if not fail:
                if result.stdout.count("SoC harness simulation passed") != 1 or "%Fatal" in result.stdout:
                    raise AssertionError(result.stdout)
                expected = {'cycles': 5}
                if stats:
                    expected.update({'rtl/cycles': 5, 'rtl/never': 0})
                if measure(result.stdout, 100) != expected:
                    raise AssertionError(result.stdout)
            elif "SoC harness simulation passed" in result.stdout or "RHODIUM-PERF:" in result.stdout:
                raise AssertionError(result.stdout)
            if cycles is not None:
                for enabled, message in ((cosim, f"test cosim closed: samples={cycles + 3}"),
                                         (trace, f"test trace closed: cycles={cycles}")):
                    if enabled and result.stdout.count(message) != 1:
                        raise AssertionError(result.stdout)
            return result.stdout

        run(cycles=5)
        uart_log = directory / 'uart.log'
        run([f'+uart-log={uart_log}', '+runtime-test-uart'], cycles=5)
        assert uart_log.read_bytes() == b'ABCD\n'
        output = run(['+uart-log=-', '+runtime-test-uart'], cycles=5)
        assert 'ABCD\n' in output
        assert 'PTY:' not in output
        run(['+uart-log='], fail=True)
        run(['+uart-log=-', '+uart-log=-'], fail=True)
        run([f'+uart-log={directory}'], fail=True)
        run(["+max-cycles=5"], cycles=5)
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
            run(["+runtime-test-drain"], cycles=8)
            run(["+runtime-test-drain", "+runtime-test-target-fail"], fail=True, cycles=8)
            run(["+runtime-test-drain", "+runtime-test-transient-exit"], cycles=8)
            run(["+runtime-test-drain", "+runtime-test-transient-exit", "+runtime-test-target-fail"], fail=True, cycles=8)
            run(["+runtime-test-drain-fail"], fail=True, cycles=5)
            run(["+runtime-test-drain-stuck", "+max-cycles=9"], fail=True, cycles=9)
            output = run(["+cosim-corrupt-order=27"], cycles=5)
            assert "test cosim opened: corruption=27" in output
            run(["+cosim-corrupt-order=invalid"], fail=True)
        else:
            run(["+cosim-corrupt-order=27"], fail=True)
        print(f"{name}: reset/settled scheduling, options, termination, and failure cleanup passed", flush=True)


if __name__ == "__main__":
    main()
