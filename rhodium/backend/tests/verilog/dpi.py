# Models held DPI results and checks foreign-call events against the native ABI fixture.
# SPDX-License-Identifier: Apache-2.0
import json
import random
import subprocess
from support import FIXTURES, run, vector_table

DPI_WIDTHS = (1, 3, 8, 16, 32, 33, 64, 65, 129)


def dpi_return_width(width):
    return width if width in (1, 3, 8, 16, 32, 64) else 32


def dpi_ports():
    inputs, outputs = [], []
    for side in ("first", "second"):
        inputs += [(f"{side}_{name}", 1) for name in ("clock", "reset", "proc_enable", "pair_enable", "single_enable")]
        for width in DPI_WIDTHS:
            inputs.append((f"{side}_data{width}", width))
            outputs += [(f"{side}_{name}{width}", size) for name, size in
                        (("inverted", width), ("tag", 8), ("result", dpi_return_width(width)),
                         ("single", dpi_return_width(width)), ("sampled", dpi_return_width(width)))]
    return inputs, outputs


def dpi_cases():
    """Track foreign result state and unordered call multisets at every edge."""
    inputs, outputs = dpi_ports()
    controls = dict.fromkeys((name for name, _ in inputs), 0)
    previous = dict.fromkeys(("first", "second"), 0)
    input_state = {(side, width): None for side in previous for width in DPI_WIDTHS}
    results = dict.fromkeys((name for name, _ in outputs), None)
    rng = random.Random(20261007)

    def observe():
        events = []
        for identity, side in enumerate(previous):
            clock = controls[f"{side}_clock"]
            if clock and not previous[side]:
                for width in DPI_WIDTHS:
                    value = input_state[side, width]
                    mask = (1 << dpi_return_width(width)) - 1
                    results[f"{side}_sampled{width}"] = results[f"{side}_result{width}"]
                    active = [kind for kind, enable in (("P", "proc_enable"), ("F", "pair_enable"), ("S", "single_enable"))
                              if controls[f"{side}_{enable}"]]
                    if width == 8 and controls[f"{side}_pair_enable"]:
                        active.append("P")  # a second call using the same import
                    for kind in active:
                        assert value is not None
                        events.append(f"dpi {kind} {identity} {width} {value:0{((width + 31) // 32) * 8}x}")
                    if "F" in active:
                        results[f"{side}_inverted{width}"] = ((1 << width) - 1) ^ value
                        results[f"{side}_tag{width}"] = identity ^ 0x5a
                        results[f"{side}_result{width}"] = (value + identity + 3) & mask
                    if "S" in active:
                        results[f"{side}_single{width}"] = (value ^ 0xa5) & mask
                    input_state[side, width] = 0 if controls[f"{side}_reset"] else controls[f"{side}_data{width}"]
            previous[side] = clock
        return (tuple(controls[name] for name, _ in inputs),
                tuple(0 if results[name] is None else results[name] for name, _ in outputs),
                tuple(0 if results[name] is None else (1 << size) - 1 for name, size in outputs),
                sorted(events))

    yield observe()
    for side in previous: controls[f"{side}_reset"] = 1
    yield observe()
    for side in previous: controls[f"{side}_clock"] = 1
    yield observe()  # define input storage without invoking foreign code
    for side in previous: controls[f"{side}_clock"] = 0
    yield observe()
    for cycle in range(64):
        for identity, side in enumerate(previous):
            enables = (cycle + identity) % 8
            for bit, name in enumerate(("proc_enable", "pair_enable", "single_enable")):
                controls[f"{side}_{name}"] = (enables >> bit) & 1
            controls[f"{side}_reset"] = int(cycle % 7 == identity)
            for width in DPI_WIDTHS:
                patterns = (0, (1 << width) - 1, 1 << (width - 1), 1 << (cycle % width), rng.getrandbits(width))
                controls[f"{side}_data{width}"] = patterns[(cycle + identity) % len(patterns)]
        yield observe()
        controls["first_clock"] = 1
        if cycle % 3 == 0: controls["second_clock"] = 1
        yield observe()
        for name in ("proc_enable", "pair_enable", "single_enable"):
            controls[f"first_{name}"] ^= 1
        controls["first_reset"] ^= 1
        for side in previous:
            for width in DPI_WIDTHS: controls[f"{side}_data{width}"] = rng.getrandbits(width)
        yield observe()  # high-clock data/enable/reset changes produce no calls
        controls["second_clock"] = 1
        yield observe()
        controls["first_clock"] = 0
        yield observe()
        controls["second_clock"] = 0
        yield observe()


def simulate_dpi(work, name, source, direct, verilator):
    inputs, outputs = dpi_ports()
    rows = list(dpi_cases())
    declarations, steps, count = vector_table(work, f"dpi_{name}", inputs, outputs,
                                             [row[:3] for row in rows], 0, masked=True)
    steps.insert(2, '  $display("dpi phase %0d", i);')
    bench = ["module dpi_tb;"]
    bench += [f"logic [{size-1}:0] {port} = '0;" for port, size in inputs]
    bench += [f"wire [{size-1}:0] {port};" for port, size in outputs]
    connections = [f".{port}({port})" for port, _ in inputs + outputs]
    bench += [f"DpiChecks dut({', '.join(connections)});"] + declarations
    bench += ["initial begin"] + steps + ['$display("PASS DPI oracle"); $finish;', "end", "endmodule"]
    sv, tb = work / f"{name}-dpi.sv", work / f"{name}-dpi_tb.sv"
    sv.write_text(source)
    tb.write_text("\n".join(bench) + "\n")
    build = work / f"{name}-dpi-build"
    run([verilator, "--binary", "--timing", "--assert", "--build-jobs", "2",
         "--top-module", "dpi_tb", "--Mdir", build, "-o", "sim", sv, tb, FIXTURES / "dpi.cpp"],
        work / f"{name}-dpi-build.log")
    output = run([build / "sim"], work / f"{name}-dpi-run.log", timeout=60)
    phase, events, samples = None, [[] for _ in rows], []
    for line in output.splitlines():
        if line.startswith("dpi phase "):
            phase = int(line.split()[-1])
        elif line.startswith("dpi "):
            if phase is None: raise RuntimeError("unexpected startup DPI call")
            events[phase].append(line)
        elif line.startswith("sample "):
            samples.append(line)
    for index, (observed, row) in enumerate(zip(events, rows)):
        if sorted(observed) != row[3]:
            raise RuntimeError(f"DPI call mismatch at {name} phase {index}: {observed} != {row[3]}")
    if len(samples) != count or "PASS DPI oracle" not in output:
        raise RuntimeError(f"incomplete DPI simulation: {name}")
    if direct:
        result = subprocess.run([verilator, "--lint-only", "--timing", "-DSYNTHESIS", "--top-module", "dpi_tb", str(sv), str(tb)],
                                cwd=work, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        (work / "dpi-synthesis-rejection.log").write_text(result.stdout)
        if result.returncode == 0 or "Rhodium DPI requires simulation" not in result.stdout:
            raise RuntimeError("DPI synthesis mode did not reject explicitly")
    print(f"{name}: {count} DPI observations and {sum(map(len, events))} foreign calls passed", flush=True)
    return samples
