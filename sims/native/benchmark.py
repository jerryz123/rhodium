#!/usr/bin/env python3
# Measures equivalent coherent boots across bounded native plans and Verilator O2.
import argparse
import hashlib
import json
import os
import pathlib
import platform
import statistics
import subprocess
import shutil
import shlex
import tempfile


def sections(path):
    rows = subprocess.check_output(["size", "-A", str(path)], text=True).splitlines()
    names = {".text", ".rodata", ".data", ".data.rel.ro", ".bss", ".eh_frame"}
    return {row.split()[0]: int(row.split()[1]) for row in rows if row.split() and row.split()[0] in names}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=pathlib.Path, help="artifacts produced by sims/native/compare.sh")
    parser.add_argument("--trials", type=int, default=7)
    parser.add_argument("--boots", type=int, default=50)
    parser.add_argument("--cpus", help="comma-separated permitted CPU IDs; default: first six available")
    parser.add_argument("--minimum-relative", type=float, help="require best native median / Verilator median to reach this ratio")
    parser.add_argument("--modes", help="comma-separated native configuration names to measure; Verilator is always included")
    parser.add_argument("--tune", action="store_true", help="compare ordering, spin epochs, and compilation budgets")
    parser.add_argument("--reuse-code", action="store_true", help="reuse existing libraries; plan identity is still checked")
    parser.add_argument("--debug", action="store_true", help="retain generated hardware validation; default benchmarks compile it out")
    parser.add_argument("--native-cflags", default="-O2", help="flags for generated C; recorded separately from the Verilator O2 baseline")
    parser.add_argument("--pgo-boots", type=int, default=0, help="GCC profile training boots before measurement; zero disables training")
    parser.add_argument("--loop-iterations", type=int, default=0, help="target countdown loop, 0 through 2047")
    args = parser.parse_args()
    if args.trials < 1 or args.boots < 1:
        parser.error("trials and boots must be positive")
    if args.pgo_boots < 0 or (args.pgo_boots and args.reuse_code):
        parser.error("PGO training requires a fresh build and a nonnegative boot count")
    if not 0 <= args.loop_iterations <= 2047:
        parser.error("loop iterations must be between 0 and 2047")
    directory = args.directory.resolve()
    cpus = list(map(int, args.cpus.split(","))) if args.cpus else sorted(os.sched_getaffinity(0))[:6]
    if not cpus or not set(cpus) <= os.sched_getaffinity(0):
        parser.error("CPU list must be nonempty and permitted by the current affinity")
    configs = [("scheduled_1", 1, 0, 0), ("scheduled_4", 4, 0, 0),
               ("compiled_1", 1, 0, 512), ("compiled_4", 4, 0, 512),
               ("compiled_word_1", 1, 69648, 512),
               ("compiled_coalesced_1", 1, 200720, 512),
               ("compiled_epochs_4", 4, 64, 512),
               ("compiled_copy_state_1", 1, 128, 512),
               ("compiled_copy_state_4", 4, 192, 512),
               ("compiled_replicated_4", 4, 576, 512),
               ("compiled_replicated_6", 6, 576, 512),
               ("compiled_replicated_8", 8, 576, 512),
               ("compiled_parallel_state_4", 4, 16960, 512),
               ("compiled_parallel_state_6", 6, 16960, 512),
               ("compiled_parallel_state_8", 8, 16960, 512),
               ("compiled_parallel_publish_4", 4, 1600, 512)]
    if args.tune:
        configs += [("compiled_original_order_1", 1, 16, 512),
                    ("compiled_original_order_4", 4, 80, 512),
                    ("compiled_epochs_2", 2, 64, 512),
                    ("compiled_components_4", 4, 96, 512),
                    ("compiled_budget128_1", 1, 0, 128),
                    ("compiled_budget128_4", 4, 64, 128),
                    ("compiled_budget256_1", 1, 0, 256),
                    ("compiled_budget256_4", 4, 64, 256)]
    if args.modes:
        requested = set(args.modes.split(','))
        unknown = requested - {config[0] for config in configs}
        if unknown:
            parser.error(f"unknown modes: {', '.join(sorted(unknown))}")
        configs = [config for config in configs if config[0] in requested]
    modes, storage, code = {}, {}, {}
    generated_cache = {}
    base_env = {k: v for k, v in os.environ.items() if k not in ("RDS_COMPILED", "RDS_FLAGS", "RDS_WORKERS", "RDS_MAX_SHAPES")}
    base_env["RDS_LOOP_ITERATIONS"] = str(args.loop_iterations)
    # Finish all generation and compilation before running any timed trial.
    # Select diagnostic roots independently from generated-C assertions.
    model = directory / "mini.rsim"
    if not args.debug:
        source_ir = directory / "mini.optimized.json"
        if not source_ir.exists():
            parser.error("release benchmarking requires mini.optimized.json for diagnostic-cone pruning")
        model = directory / "bench-release.rsim"
        optimizer = os.getenv("RDS_OPTIMIZER", str(pathlib.Path(__file__).resolve().parents[2] / "rhodium/sim/compiler/run.sh"))
        subprocess.run([optimizer, "--input", str(source_ir), "--release", "--no-optimize",
                        "--output", str(model)], check=True)
    verilator_dir = directory / ("verilated-mini" if args.debug else "verilated-mini-release")
    verilator = verilator_dir / "VSoCHarness"
    if not args.debug and not args.reuse_code:
        with (directory / "benchmark-verilator-build.log").open("w") as log:
            subprocess.run([os.getenv("VERILATOR", "verilator"), "--cc", "-O2", "--threads", "1", "--no-assert",
                            "--Wno-UNOPTFLAT", "--Wno-SYMRSVDWORD", "--top-module", "SoCHarness",
                            "--Mdir", str(verilator_dir), "--exe",
                            str(pathlib.Path(__file__).resolve().parent / "verilator-smoke.cpp"),
                            "-CFLAGS", "-O2 -DNDEBUG -DRDS_VERILATOR_THREADS=1",
                            "-MAKEFLAGS", "OPT_FAST=-O2 OPT_SLOW=-O2 OPT_GLOBAL=-O2", "--build", "-j", "2",
                            str(directory / "mini.sv")], stdout=log, stderr=subprocess.STDOUT, check=True)
    if not verilator.exists():
        parser.error(f"missing {verilator}; build once without --reuse-code")
    for label, workers, flags, shapes in configs:
        env = dict(base_env, RDS_WORKERS=str(workers), RDS_FLAGS=str(flags), RDS_MAX_SHAPES=str(shapes))
        if shapes:
            source, binary = directory / f"bench-{label}.c", directory / f"bench-{label}.so"
            if not args.reuse_code:
                subprocess.run([str(directory / "compile-model"), str(model), str(source)], env=env, check=True)
                identity = hashlib.sha256(source.read_bytes()).digest()
                if identity in generated_cache and not args.pgo_boots:
                    shutil.copyfile(generated_cache[identity], binary)
                else:
                    compiler = [os.getenv("CC", "cc"), "-std=c17", *shlex.split(args.native_cflags), "-UNDEBUG" if args.debug else "-DNDEBUG", "-fPIC", "-shared", str(source), "-o", str(binary)]
                    if args.pgo_boots:
                        profile = pathlib.Path(tempfile.mkdtemp(prefix=f"pgo-{label}-", dir=directory))
                        subprocess.run(compiler + [f"-fprofile-generate={profile}", "-fprofile-update=atomic"], check=True)
                        training_env = dict(env, RDS_COMPILED=str(binary), RDS_LOOP_ITERATIONS="0")
                        subprocess.run(["taskset", "-c", ",".join(map(str, cpus[:1] if workers == 1 else cpus)),
                                        str(directory / "mini-smoke"), str(model), str(args.pgo_boots), "bench"],
                                       env=training_env, check=True, stdout=subprocess.DEVNULL)
                        subprocess.run(compiler + [f"-fprofile-use={profile}", "-fprofile-correction", "-Werror=missing-profile"], check=True)
                    else:
                        subprocess.run(compiler, check=True)
                    generated_cache[identity] = binary
            env["RDS_COMPILED"] = str(binary)
            code[label] = sections(binary)
        command = ["taskset", "-c", ",".join(map(str, cpus[:1] if workers == 1 else cpus)),
                   str(directory / "mini-smoke"), str(model)]
        modes[label] = (command, env)
        storage[label] = json.loads(subprocess.check_output(command + ["0", "stats"], env=env, text=True))
        print(f"prepared {label}: {storage[label]['workers']} workers", flush=True)
    modes["verilator_O2"] = (["taskset", "-c", str(cpus[0]), str(verilator)], base_env)
    code["native_runtime"] = sections(directory / "mini-smoke")
    code["verilator_O2"] = sections(verilator)
    rows, expected = [], None
    for trial in range(args.trials):
        order = list(modes)
        order = order[trial % len(order):] + order[:trial % len(order)]
        if trial % 2:
            order.reverse()
        for label in order:
            command, env = modes[label]
            row = json.loads(subprocess.check_output(command + [str(args.boots), "bench"], env=env, text=True))
            observed = row["cycles"], row["polls"], row["digest"]
            if expected is None:
                expected = observed
            assert observed == expected, (label, observed, expected)
            row.update(mode=label, trial=trial, cycles_per_second=row["cycles"] / row["seconds"])
            rows.append(row)
            print(json.dumps(row), flush=True)
    report = dict(diagnostic_cones_retained=args.debug, verilator_assertions=args.debug, model_path=str(model), host=platform.platform(), cpus=cpus, trials=args.trials, boots=args.boots, loop_iterations=args.loop_iterations,
                  toolchain={"cc": subprocess.check_output([os.getenv("CC", "cc"), "--version"], text=True).splitlines()[0],
                             "verilator": subprocess.check_output([os.getenv("VERILATOR", "verilator"), "--version"], text=True).strip()},
                  artifact_sha256={str(path.relative_to(directory)): hashlib.sha256(path.read_bytes()).hexdigest()
                                   for path in [model, directory / "mini-smoke", verilator]
                                   + sorted(directory.glob("bench-*.so"))},
                  baseline="Verilator -O2, all generated C++ optimization variables at O2",
                  native_cflags=None if args.reuse_code else args.native_cflags + (" -UNDEBUG" if args.debug else " -DNDEBUG"), native_debug=None if args.reuse_code else args.debug, pgo_training_boots=args.pgo_boots, pgo_training_loop_iterations=0,
                  warmup_boots=5, startup_excluded=True, rows=rows, storage=storage, executable_sections=code,
                  median_cycles_per_second={label: statistics.median(r["cycles_per_second"] for r in rows if r["mode"] == label) for label in modes})
    medians = report["median_cycles_per_second"]
    report["best_native_relative"] = max(value for mode, value in medians.items() if mode != "verilator_O2") / medians["verilator_O2"]
    report["checks"] = {"rtl_assertions": args.debug, "strict_partial_operations": False, "functional_payloads": True, "waveforms": False, "sanitizers": False}
    filename = f"benchmark-loop{args.loop_iterations}.json" if args.loop_iterations else "benchmark.json"
    (directory / filename).write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report["median_cycles_per_second"], indent=2))
    if args.minimum_relative is not None and report["best_native_relative"] < args.minimum_relative:
        raise SystemExit(f"native/Verilator ratio {report['best_native_relative']:.3f} is below {args.minimum_relative:.3f}")


if __name__ == "__main__":
    main()
