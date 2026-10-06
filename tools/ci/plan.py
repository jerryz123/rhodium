#!/usr/bin/env python3
# Computes the dependency-aware GitHub Actions plan from changed paths.
# SPDX-License-Identifier: Apache-2.0

import argparse
import fnmatch
import json
import subprocess
from dataclasses import dataclass, field

from .policy import CHECKS, CIRCT_CHECKS, CIRCT_CORE_CHECKS, HOST_CHECKS, NATIVE_SUITES, SIMULATOR_CONFIGS, SINGLE_CORE_SOCS, COSIM_CONFIGS, native_configs, simulation_entry, simulator_entry, BACKEND_SMOKE_CONFIG, BACKEND_SMOKE_VARIANTS, arch_configs, arch_shards


def matches(path, *patterns):
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


@dataclass
class Selection:
    checks: set[str] = field(default_factory=set)
    native_suites: set[str] = field(default_factory=set)
    simulation: bool = False
    arch: bool = False

    def add_checks(self, *keys):
        self.checks.update(keys)
        if "circt-verilog-differential" in keys:
            # Authored comparisons reuse these lanes' CIRCT behavioral oracles.
            self.checks.update(("circt-language", "circt-std", "circt-protocols"))

    def add_native(self, *suites):
        self.native_suites.update(suites)

    def all_programs(self):
        self.add_native(*NATIVE_SUITES)
        self.arch = True

    def all(self):
        self.add_checks(*(check.key for check in CHECKS))
        self.simulation = True
        self.all_programs()

    def classify(self, path):
        documentation = matches(path, "*.md", "LICENSE", "LICENSE.*", "NOTICE", "DCO", "AGENTS.md", ".gitignore", ".gitattributes")

        # Native workloads and ACT cover selected single-core configs independently
        # of host, example, and CIRCT checks.
        if documentation or matches(path, "tools/emacs/*"):
            pass
        elif path == "sw/build/isa.mk":
            self.add_native("isa")
        elif matches(path, "sw/build/build-coremark.py", "sw/coremark-riscv-baremetal/*", "sw/coremark", "sw/coremark/*"):
            self.add_native("coremark")
        elif matches(path, "sw/build/build-embench.py", "sw/embench-iot-riscv-baremetal/*", "sw/embench-iot", "sw/embench-iot/*"):
            self.add_native("embench")
        elif matches(path, "sw/build/build-bringup-bench.py", "sw/bringup-bench-riscv-baremetal/*", "sw/bringup-bench-patches/*", "sw/bringup-bench", "sw/bringup-bench/*", "sw/tests/test_bringup_bench.py"):
            self.add_native("bringup")
        elif matches(path, "sims/arch-test/*", "sims/tests/test_arch_test.py", "sims/sail/*", "sims/tests/test_sail_config.py", "sw/riscv-arch-test", "sw/riscv-arch-test/*", "sw/riscv-arch-test-patches/*", "riscv/sail-riscv", "riscv/sail-riscv/*", "riscv/sail-riscv-patches/*", "tools/write-riscv-udb-config.rhm"):
            self.arch = True
        elif matches(path, "riscv/patched_submodule.py", "riscv/tests/test_patched_submodule.py", "riscv/riscv-isa-sim", "riscv/riscv-isa-sim/*", "riscv/riscv-isa-sim-patches/*"):
            self.all_programs()
        elif matches(path, "sw/riscv-isa-tests", "sw/riscv-isa-tests/*", "sw/build/build.py"):
            self.add_native("isa", "benchmark")
        elif matches(path, "sw/build/program_target.py", "sw/build/bind.py", "sw/tests/test_program_build.py"):
            self.all_programs()
        elif matches(path, "sw/opensbi", "sw/opensbi/*", "sw/build/opensbi.py", "sw/tests/test_opensbi_build.py", "sims/opensbi/*"):
            pass
        elif matches(path, "rhodium/core/*", "rhodium/lowering/*", "rhodium/frontend/*", "rhodium/base/*", "rhodium/std/*", "rhodium/backend/*", "rhodium/compile/*", "rhodium/language.rhm", "rhodium/main.rkt", "flow/*", "cores/*", "riscv/*", "hardfloat/*", "chi/*", "noc/*", "devices/*", "socs/*", "sims/*", "support/annotations.rhm", "devicetree/*", "tools/install-circt.sh", "tools/install-riscv-toolchain.sh", ".github/actions/setup-riscv-toolchain/*"):
            self.all_programs()

        # Authored backend integrations reuse SyncRam, UART, and Flow event
        # scoreboards. Their dependencies must select both emission routes.
        if not documentation and matches(path, "rhodium/std/*", "devices/uart/uart.rhdl",
                                         "devices/uart/uart-dpi.rhdl", "devices/uart/dpi/*",
                                         "devices/tests/uart-dpi*", "devices/tests/circt/verilog/uart-dpi*",
                                         "examples/std/sync-ram.rhdl", "flow/*", "rhodium/event/*", "rheg/*"):
            self.add_checks("verilog-direct", "circt-verilog-differential")

        # The native rsim smoke also reuses the production UART and PTY helpers.
        if not documentation and matches(path, "devices/uart/uart.rhdl", "devices/uart/uart-dpi.rhdl",
                                         "devices/uart/dpi/*", "devices/tests/circt/verilog/uart-dpi_dpi.cpp"):
            self.add_checks("host-backend")

        # The SV-hosted rsim memory qualification uses production CHI RTL and DPI.
        if not documentation and matches(path, "chi/protocol/*", "chi/subordinate/*"):
            self.add_checks("circt-verilog-differential")

        # The rsim SV binding proof runs the unchanged production clock driver.
        if path in ("sims/TestDriver.v", "sims/verilator/simulation_runtime.cc", "sims/verilator/simulation_runtime.h"):
            self.add_checks("circt-verilog-differential")

        if path.endswith((".rhm", ".rhdl")) and not matches(path, "tools/emacs/*"):
            self.add_checks("host-hygiene")

        if documentation:
            return
        if matches(path, "sram/*", "vlsi/sim/*", "vlsi/designs/mini-rv5stage-soc/sky130/*"):
            self.simulation = True
        elif matches(path, "tools/emacs/*", "vlsi/*"):
            return
        elif matches(path, ".github/workflows/*", ".github/actions/*", "tools/ci/*"):
            self.all()
        elif matches(path, "sims/arch-test/*.rhm", "sims/tests/config-test.rhm"):
            self.add_checks("host-socs")
        elif matches(path, "sims/arch-test/*", "sims/tests/test_arch_test.py", "sims/sail/*", "sims/tests/test_sail_config.py",
                     "sw/build/build-coremark.py", "sw/build/build-embench.py",
                     "sw/build/build-bringup-bench.py", "sw/coremark", "sw/coremark/*",
                     "sw/coremark-riscv-baremetal/*", "sw/embench-iot", "sw/embench-iot/*",
                     "sw/embench-iot-riscv-baremetal/*", "sw/bringup-bench",
                     "sw/bringup-bench/*", "sw/bringup-bench-riscv-baremetal/*",
                     "sw/bringup-bench-patches/*", "sw/tests/test_bringup_bench.py"):
            pass
        elif matches(path, "sims/program-test/*", "sims/tests/test_program_test.py", "sw/build/*", "sw/coremark*", "sw/embench-iot*", "sw/bringup-bench*", "sw/litmus-*", "sw/tests/test_program_build.py", "sw/tests/test_bringup_bench.py", "sw/tests/test_litmus_build.py", "sw/riscv-isa-tests", "sw/riscv-isa-tests/*", "tools/install-riscv-toolchain.sh"):
            self.simulation = True
        elif matches(path, "sw/opensbi", "sw/opensbi/*", "sw/tests/test_opensbi_build.py", "sims/opensbi/*"):
            self.simulation = True
        elif matches(path, "sw/riscv-arch-test", "sw/riscv-arch-test/*", "sw/riscv-arch-test-patches/*", "riscv/sail-riscv", "riscv/sail-riscv/*", "riscv/sail-riscv-patches/*", "sims/arch-test/*", "sims/tests/test_arch_test.py", "sims/sail/*", "sims/tests/test_sail_config.py"):
            pass
        elif matches(path, "riscv/patched_submodule.py", "riscv/tests/test_patched_submodule.py"):
            self.add_checks("host-models", "host-backend", "host-hygiene")
            self.simulation = True
        elif matches(path, "riscv/riscv-isa-sim", "riscv/riscv-isa-sim/*", "riscv/riscv-isa-sim-patches/*"):
            self.add_checks("host-backend", "host-hygiene")
            self.simulation = True
        elif path == "Makefile":
            self.all()
        elif path == "tools/check-example-verilog.sh":
            self.add_checks("host-hygiene", "host-examples", *CIRCT_CHECKS)
        elif matches(path, "tools/testing/circt/*"):
            self.add_checks("host-hygiene", "host-examples", *CIRCT_CHECKS)
        elif path == "tools/testing/run-negative.rkt":
            self.add_checks(*HOST_CHECKS)
        elif path == "tools/run-racket-tests.sh":
            self.add_checks(*HOST_CHECKS, "verilog-direct")
            self.simulation = True
            self.all_programs()
        elif matches(path, "tools/run-racket.sh", "tools/racket-build-cache.sh", "tools/refresh-racket-project-cache.sh", "tools/invalidate-racket-build-cache.rkt"):
            self.add_checks(*HOST_CHECKS, "verilog-direct", *CIRCT_CHECKS)
            self.simulation = True
            self.all_programs()
        elif path == "tools/testing/racket-build-cache-test.sh":
            self.add_checks("host-hygiene")
        elif path == "tools/write-rv5stage-core-diagram.rhm":
            self.add_checks("host-examples")
        elif path == "tools/write-riscv-udb-config.rhm":
            self.add_checks("host-models", "host-cores", "host-socs")
        elif path == "tools/write-noc-router-diagram.rhm":
            self.add_checks("host-examples")
        elif matches(path, ".githooks/pre-commit", "tools/check-license-headers.sh", "tools/check-parameter-annotations.rkt", "tools/parameter-annotation-scope.txt", "tools/check-boundaries.sh", "rfpl/check-boundaries.sh", "noc/check-boundaries.sh", "riscv/check-boundaries.sh", "chi/check-boundaries.sh", "cores/check-boundaries.sh", "socs/check-boundaries.sh"):
            self.add_checks("host-hygiene")
        elif matches(path, "rhodium/event/*", "rheg/*"):
            self.add_checks("host-foundation", "host-backend", "circt-language")
            self.simulation = True
        elif matches(path, "rhodium/core/*", "rhodium/lowering/*", "rhodium/analysis/*", "rhodium/frontend/*", "rhodium/base/*", "rhodium/language.rhm", "rhodium/main.rkt"):
            self.all()
        elif matches(path, "rhodium/std/*", "flow/*"):
            self.add_checks("host-hygiene", "host-foundation", "host-backend", "host-protocols", "host-cores", "host-socs", "host-examples", "circt-language", "circt-std", "circt-protocols", *CIRCT_CORE_CHECKS)
            self.simulation = True
        elif matches(path, "rhodium/backend/*", "rhodium/compile/*"):
            self.add_checks("host-backend", "verilog-direct", *CIRCT_CHECKS)
            self.simulation = True
        elif matches(path, "examples/rtl/*"):
            self.add_checks("host-examples", "circt-language")
        elif matches(path, "examples/clocking/*"):
            self.add_checks("host-examples", "circt-language")
        elif matches(path, "examples/formal/*"):
            self.add_checks("host-foundation")
        elif matches(path, "examples/std/*"):
            self.add_checks("host-examples", "circt-std")
        elif matches(path, "examples/noc/*"):
            self.add_checks("host-examples", "circt-protocols")
        elif matches(path, "examples/lop/*"):
            self.add_checks("host-examples", "host-foundation", "host-backend", "circt-language")
        elif matches(path, "examples/rfpl/*"):
            self.add_checks("host-examples", "circt-rfpl")
        elif matches(path, "examples/riscv/*"):
            self.add_checks("host-examples", "circt-core-components")
        elif matches(path, "examples/chi/*"):
            self.add_checks("host-examples", "circt-protocols")
        elif matches(path, "examples/cores/*"):
            self.add_checks("host-examples", "circt-core-components", "circt-core-execution-datapath")
        elif matches(path, "examples/rv5stage/*"):
            self.add_checks("host-examples")
        elif matches(path, "examples/*"):
            self.all()
        elif matches(path, "rfpl/*"):
            self.add_checks("host-protocols", "circt-rfpl", "host-examples")
        elif matches(path, "noc/*"):
            self.add_checks("host-models", "host-socs", "circt-protocols", "host-examples")
        elif matches(path, "riscv/*"):
            self.add_checks("host-models", "host-cores", "host-socs", "host-examples", *CIRCT_CORE_CHECKS)
        elif matches(path, "hardfloat/*"):
            self.add_checks("host-models", *CIRCT_CORE_CHECKS)
            self.simulation = True
        elif matches(path, "devicetree/*"):
            self.add_checks("host-models")
        elif matches(path, "chi/subordinate/memory-controller.rhdl", "chi/subordinate/dpi-memory.rhdl", "chi/subordinate/dpi/*", "chi/tests/dpi-memory-*", "chi/tests/chi_dpi_memory_*"):
            self.add_checks("host-protocols", "host-socs", "circt-protocols", "host-examples")
            self.simulation = True
        elif matches(path, "chi/*"):
            self.add_checks("host-protocols", "host-socs", "circt-protocols", "host-examples")
        elif matches(path, "cores/*"):
            self.add_checks("host-cores", "host-socs", "host-examples", *CIRCT_CORE_CHECKS)
            self.simulation = True
        elif matches(path, "sims/fesvr/*.rhdl"):
            self.add_checks("circt-protocols")
            self.simulation = True
        elif matches(path, "sims/cosim/*"):
            self.add_checks("host-socs", "circt-core-components", "circt-core-execution-datapath")
            self.simulation = True
        elif matches(path, "sims/*"):
            self.simulation = True
        elif matches(path, "socs/*"):
            self.add_checks("host-socs", *CIRCT_CORE_CHECKS)
            self.simulation = True
        elif matches(path, "support/annotations.rhm", "support/tests/*"):
            self.add_checks("host-foundation")
        elif path == "support/README.md":
            self.add_checks("host-foundation")
        elif path == "tools/install-circt.sh":
            self.add_checks(*CIRCT_CHECKS)
            self.simulation = True
        else:
            self.all()

    def result(self):
        matrix = {"include": [check.matrix_entry() for check in CHECKS if check.key in self.checks]}
        suites = {"include": []}
        for suite in NATIVE_SUITES:
            if suite not in self.native_suites:
                continue
            suites["include"].extend({"soc": soc, "suite": suite, "cosim": soc in COSIM_CONFIGS} for soc in native_configs(suite))
            if suite == "coremark":
                suites["include"].extend({"soc": soc, "suite": "coremark_scalar", "cosim": soc in COSIM_CONFIGS} for soc in native_configs(suite))
        run_checks = bool(self.checks)
        run_program_native = bool(self.native_suites)
        run_simulator = self.simulation or run_program_native or self.arch
        configs = (SIMULATOR_CONFIGS if self.simulation else
                    tuple(config for config in SIMULATOR_CONFIGS if config[0] in SINGLE_CORE_SOCS))
        entry = simulation_entry if self.simulation else simulator_entry
        builds = [entry(*config) for config in configs] if run_simulator else []
        if self.simulation:
            builds.extend(simulation_entry(*BACKEND_SMOKE_CONFIG, backend=backend)
                          for backend in BACKEND_SMOKE_VARIANTS)
        return {
            "run_compile": run_checks or run_simulator,
            "run_sail": self.arch or any(build["cosim"] for build in builds),
            "run_checks": run_checks,
            "checks_matrix": matrix,
            "run_simulator": run_simulator,
            "simulator_matrix": {"include": builds},
            "run_simulation": self.simulation,
            "run_program_native": run_program_native,
            "program_matrix": suites,
            "run_program_arch": self.arch,
            "arch_build_matrix": {"include": list(arch_configs()) if self.arch else []},
            "arch_run_matrix": {"include": list(arch_shards()) if self.arch else []},
        }


def plan_for_paths(paths):
    selection = Selection()
    for path in paths:
        selection.classify(path)
    return selection.result()


def changed_paths(base_revision, head_revision):
    for revision in (base_revision, head_revision):
        if not revision or set(revision) == {"0"}:
            return None
        if subprocess.run(["git", "cat-file", "-e", f"{revision}^{{commit}}"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            return None
    result = subprocess.run(["git", "diff", "--name-only", base_revision, head_revision], text=True, capture_output=True)
    return result.stdout.splitlines() if result.returncode == 0 else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--all", action="store_true", help="select every CI lane")
    source.add_argument("--paths", nargs="*", help="classify these repository-relative paths")
    source.add_argument("--revisions", nargs=2, metavar=("BASE", "HEAD"), help="classify paths changed between two commits")
    parser.add_argument("--pretty", action="store_true", help="indent the JSON output")
    args = parser.parse_args()

    if args.all:
        selection = Selection()
        selection.all()
        plan = selection.result()
    elif args.paths is not None:
        plan = plan_for_paths(args.paths)
    else:
        paths = changed_paths(*args.revisions)
        if paths is None:
            selection = Selection()
            selection.all()
            plan = selection.result()
        else:
            plan = plan_for_paths(paths)
    print(json.dumps(plan, indent=2 if args.pretty else None, separators=None if args.pretty else (",", ":")))


if __name__ == "__main__":
    main()
