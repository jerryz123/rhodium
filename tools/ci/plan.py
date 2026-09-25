#!/usr/bin/env python3
# Computes the dependency-aware GitHub Actions plan from changed paths.
# SPDX-License-Identifier: Apache-2.0

import argparse
import fnmatch
import json
import subprocess
from dataclasses import dataclass, field

from .policy import CHECKS, CIRCT_CHECKS, CIRCT_CORE_CHECKS, EXAMPLE_CHECKS, HOST_CHECKS, NATIVE_SUITES, SIMULATOR_PRODUCTS, SINGLE_CORE_SOCS, simulation_entry


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

    def add_native(self, *suites):
        self.native_suites.update(suites)

    def all_programs(self):
        self.add_native(*NATIVE_SUITES)
        self.arch = True

    def all(self):
        self.add_checks(*(HOST_CHECKS | EXAMPLE_CHECKS | CIRCT_CHECKS))
        self.simulation = True
        self.all_programs()

    def classify(self, path):
        documentation = matches(path, "*.md", "LICENSE", "LICENSE.*", "NOTICE", "DCO", "AGENTS.md", ".gitignore", ".gitattributes")

        # Native workloads and ACT cover both single-core products independently
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
        elif matches(path, "sims/arch-test/*", "sims/tests/test_arch_test.py", "sw/riscv-arch-test", "sw/riscv-arch-test/*", "sw/riscv-arch-test-patches/*", "tools/write-riscv-udb-config.rhm"):
            self.arch = True
        elif matches(path, "riscv/patched_submodule.py", "riscv/tests/test_patched_submodule.py", "riscv/riscv-isa-sim", "riscv/riscv-isa-sim/*", "riscv/riscv-isa-sim-patches/*"):
            self.all_programs()
        elif matches(path, "sw/riscv-isa-tests", "sw/riscv-isa-tests/*", "sw/build/build.py"):
            self.add_native("isa", "benchmark")
        elif matches(path, "sw/build/program_target.py", "sw/tests/test_program_build.py"):
            self.all_programs()
        elif matches(path, "sw/opensbi", "sw/opensbi/*", "sw/build/opensbi.py", "sw/tests/test_opensbi_build.py", "sims/opensbi/*"):
            pass
        elif matches(path, "rhodium/core/*", "rhodium/frontend/*", "rhodium/base/*", "rhodium/std/*", "rhodium/backend/*", "rhodium/language.rhm", "rhodium/main.rkt", "flow/*", "cores/*", "riscv/*", "hardfloat/*", "chi/*", "noc/*", "devices/*", "socs/*", "sims/*", "support/annotations.rhm", "devicetree/*", "tools/install-circt.sh", "tools/install-riscv-toolchain.sh", ".github/actions/setup-riscv-toolchain/*"):
            self.all_programs()

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
        elif matches(path, "sims/arch-test/*", "sims/tests/test_arch_test.py",
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
        elif matches(path, "sw/riscv-arch-test", "sw/riscv-arch-test/*", "sw/riscv-arch-test-patches/*", "sims/arch-test/*", "sims/tests/test_arch_test.py"):
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
            self.add_checks("host-hygiene", *CIRCT_CHECKS, *EXAMPLE_CHECKS)
        elif matches(path, "tools/testing/circt/*"):
            self.add_checks(*CIRCT_CHECKS)
        elif path == "tools/testing/run-negative.rkt":
            self.add_checks(*HOST_CHECKS)
        elif path == "tools/run-racket-tests.sh":
            self.add_checks(*HOST_CHECKS, *EXAMPLE_CHECKS)
            self.simulation = True
            self.all_programs()
        elif matches(path, "tools/run-racket.sh", "tools/racket-build-cache.sh", "tools/invalidate-racket-build-cache.rkt"):
            self.add_checks(*HOST_CHECKS, *CIRCT_CHECKS, *EXAMPLE_CHECKS)
            self.simulation = True
            self.all_programs()
        elif path == "tools/testing/racket-build-cache-test.sh":
            self.add_checks("host-hygiene")
        elif path == "tools/write-rv5stage-core-diagram.rhm":
            self.add_checks("example-rv5stage")
        elif path == "tools/write-riscv-udb-config.rhm":
            self.add_checks("host-models", "host-cores", "host-socs")
        elif path == "tools/write-noc-router-diagram.rhm":
            self.add_checks("example-noc")
        elif matches(path, ".githooks/pre-commit", "tools/check-license-headers.sh", "tools/check-parameter-annotations.rkt", "tools/parameter-annotation-scope.txt", "tools/check-boundaries.sh", "rfpl/check-boundaries.sh", "noc/check-boundaries.sh", "riscv/check-boundaries.sh", "chi/check-boundaries.sh", "cores/check-boundaries.sh", "socs/check-boundaries.sh"):
            self.add_checks("host-hygiene")
        elif matches(path, "rhodium/event/*", "rheg/*"):
            self.add_checks("host-foundation", "host-backend", "circt-language")
            self.simulation = True
        elif matches(path, "rhodium/core/*", "rhodium/analysis/*", "rhodium/frontend/*", "rhodium/base/*", "rhodium/language.rhm", "rhodium/main.rkt"):
            self.all()
        elif matches(path, "rhodium/std/*", "flow/*"):
            self.add_checks("host-hygiene", "host-foundation", "host-backend", "host-protocols", "host-cores", "host-socs", "circt-language", "circt-std", "circt-protocols", *CIRCT_CORE_CHECKS, "example-rtl", "example-clocking", "example-std", "example-noc", "example-riscv", "example-chi", "example-cores", "example-rv5stage")
            self.simulation = True
        elif matches(path, "rhodium/backend/*"):
            self.add_checks("host-backend", *CIRCT_CHECKS)
            self.simulation = True
        elif matches(path, "examples/rtl/*"):
            self.add_checks("example-rtl", "circt-language")
        elif matches(path, "examples/clocking/*"):
            self.add_checks("example-clocking")
        elif matches(path, "examples/formal/*"):
            self.add_checks("host-foundation")
        elif matches(path, "examples/std/*"):
            self.add_checks("example-std", "circt-std")
        elif matches(path, "examples/noc/*"):
            self.add_checks("example-noc", "circt-protocols")
        elif matches(path, "examples/lop/*"):
            self.add_checks("example-lop", "circt-language")
        elif matches(path, "examples/rfpl/*"):
            self.add_checks("example-rfpl", "circt-rfpl")
        elif matches(path, "examples/riscv/*"):
            self.add_checks("example-riscv", *CIRCT_CORE_CHECKS)
        elif matches(path, "examples/chi/*"):
            self.add_checks("example-chi", "circt-protocols")
        elif matches(path, "examples/cores/*"):
            self.add_checks("example-cores", *CIRCT_CORE_CHECKS)
        elif matches(path, "examples/rv5stage/*"):
            self.add_checks("example-rv5stage")
        elif matches(path, "examples/*"):
            self.all()
        elif matches(path, "rfpl/*"):
            self.add_checks("host-protocols", "circt-rfpl", "example-rfpl")
        elif matches(path, "noc/*"):
            self.add_checks("host-models", "host-socs", "circt-protocols", "example-noc")
        elif matches(path, "riscv/*"):
            self.add_checks("host-models", "host-cores", "host-socs", *CIRCT_CORE_CHECKS, "example-riscv", "example-cores", "example-rv5stage")
        elif matches(path, "hardfloat/*"):
            self.add_checks("host-models", *CIRCT_CORE_CHECKS)
            self.simulation = True
        elif matches(path, "devicetree/*"):
            self.add_checks("host-models")
        elif matches(path, "chi/subordinate/memory-controller.rhdl", "chi/subordinate/dpi-memory.rhdl", "chi/subordinate/dpi/*", "chi/tests/dpi-memory-*", "chi/tests/chi_dpi_memory_*"):
            self.add_checks("host-protocols", "host-socs", "circt-protocols", "example-chi")
            self.simulation = True
        elif matches(path, "chi/*"):
            self.add_checks("host-protocols", "host-socs", "circt-protocols", "example-chi")
        elif matches(path, "cores/*"):
            self.add_checks("host-cores", "host-socs", *CIRCT_CORE_CHECKS, "example-cores", "example-rv5stage")
            self.simulation = True
        elif matches(path, "sims/fesvr/*.rhdl"):
            self.add_checks("circt-protocols")
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
            suites["include"].extend({"soc": soc, "suite": suite} for soc in SINGLE_CORE_SOCS)
            if suite == "coremark":
                suites["include"].extend({"soc": soc, "suite": "coremark_scalar"} for soc in SINGLE_CORE_SOCS)
        run_checks = bool(self.checks)
        run_program_native = bool(self.native_suites)
        run_simulator = self.simulation or run_program_native or self.arch
        products = (SIMULATOR_PRODUCTS if self.simulation else
                    tuple(product for product in SIMULATOR_PRODUCTS if product[0] in SINGLE_CORE_SOCS))
        return {
            "run_compile": run_checks or run_simulator,
            "run_checks": run_checks,
            "checks_matrix": matrix,
            "run_simulator": run_simulator,
            "simulator_matrix": {"include": [dict(soc=soc, shape=shape, core=core)
                                             for soc, shape, core in products]} if run_simulator else {"include": []},
            "run_simulation": self.simulation,
            "simulation_matrix": {"include": [simulation_entry(soc, shape, core)
                                            for soc, shape, core in SIMULATOR_PRODUCTS]} if self.simulation else {"include": []},
            "run_program_native": run_program_native,
            "program_matrix": suites,
            "run_program_arch": self.arch,
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
