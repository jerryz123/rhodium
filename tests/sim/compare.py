#!/usr/bin/env python3
# Verifies full traces and measures execution, storage, and code against Verilator O2.
import json
import os
import pathlib
import platform
import statistics
import subprocess
import sys
from runtime_test import Native, Options, library


def run(command, env):
    lines = subprocess.check_output(command, text=True, env=env).splitlines()
    tag, digest, seconds = lines[-1].split()
    assert tag == "result"
    return lines[:-1], digest, float(seconds)


def sections(path):
    rows = subprocess.check_output(["size", "-A", str(path)], text=True).splitlines()
    return {row.split()[0]: int(row.split()[1]) for row in rows if row.startswith((".text ", ".rodata ", ".data ", ".bss "))}


directory = pathlib.Path(sys.argv[1])
cycles = int(sys.argv[2])
fixture = sys.argv[3] if len(sys.argv) > 3 else "hierarchy"
top = {"hierarchy": "Hierarchy", "pipeline": "Pipeline"}[fixture]
base_env = dict(os.environ, RDS_FLAGS="0", RDS_WORKERS="1")
commands = {}
for label, flags, workers in [("native_reference", 1, 1), ("native_no_reuse", 2, 1),
                              ("native_generic", 4, 1), ("native", 0, 1), ("native_4_workers", 0, 4)]:
    model = fixture + ("-raw" if flags == 1 else "")
    commands[label] = ([str(directory / "native-driver"), str(directory / f"{model}.rsim")],
                       dict(base_env, RDS_FLAGS=str(flags), RDS_WORKERS=str(workers)))
commands["verilator_O2"] = ([str(directory / f"verilated-{fixture}/V{top}")], base_env)
lib = library(directory / "librhodium_sim.so")
lib.compiled, lib.compiled_cache, lib.shape_limit = True, {}, 512
for label, workers, flags in [("compiled_1", 1, 0), ("compiled_epochs_4", 4, 64)]:
    lib.options = Options(workers, flags)
    model = directory / f"{fixture}.rsim"
    instance = Native(lib, model)
    binary = lib.compiled_cache[(model, workers, flags, 512)]
    instance.close()
    commands[label] = ([str(directory / "native-driver"), str(model)],
                       dict(base_env, RDS_FLAGS=str(flags), RDS_WORKERS=str(workers), RDS_COMPILED=str(binary)))
expected_trace = expected_digest = None
storage = {}
for label, (command, env) in commands.items():
    trace, digest, _ = run(command + ["10000", "trace"], env)
    if expected_trace is None:
        expected_trace, expected_digest = trace, digest
    assert trace == expected_trace and digest == expected_digest, f"{label}: cycle traces differ"
    if label != "verilator_O2":
        lines, _, _ = run(command + ["0", "stats"], env)
        storage[label] = dict(zip(("model_file_bytes", "descriptor_bytes", "schedule_bytes", "value_bytes",
                                  "memory_bytes", "update_bytes", "workers", "components", "batches"),
                                 map(int, lines[0].split()[1:])))
measurements = {label: [] for label in commands}
digest = None
# Rotate and reverse ordering to reduce systematic warm-up and host drift bias.
for repeat in range(5):
    order = list(commands)
    order = order[repeat:] + order[:repeat]
    if repeat % 2:
        order.reverse()
    for label in order:
        command, env = commands[label]
        _, result, seconds = run(command + [str(cycles)], env)
        if digest is None:
            digest = result
        assert result == digest, f"{label}: benchmark checksums differ"
        measurements[label].append(seconds)
report = {
    "workload": fixture, "host": platform.platform(), "cpu": platform.processor(),
    "baseline": "Verilator -O2; generated C++ OPT_FAST/OPT_SLOW/OPT_GLOBAL=-O2",
    "trace_cycles_compared": len(expected_trace), "timed_cycles": cycles, "checksum": digest,
    "seconds": measurements,
    "median_cycles_per_second": {k: cycles / statistics.median(v) for k, v in measurements.items()},
    "speedup_over_verilator_O2": {k: statistics.median(measurements["verilator_O2"]) / statistics.median(v)
                                 for k, v in measurements.items()},
    "storage": storage,
    "executable_sections": {"native": sections(directory / "native-driver"),
                            "verilator_O2": sections(directory / f"verilated-{fixture}/V{top}")},
    "note": "Single process per run; no CPU pinning. Storage excludes allocator metadata and pthread stacks.",
}
print(json.dumps(report, indent=2))
