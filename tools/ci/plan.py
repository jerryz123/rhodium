#!/usr/bin/env python3
# Computes the dependency-aware GitHub Actions plan from changed paths.
# SPDX-License-Identifier: Apache-2.0

import argparse
import fnmatch
import json
import subprocess
from dataclasses import dataclass, field

from .policy import CHECKS, RSIM_CORE_CHECKS, RSIM_COMPONENT_CHECKS, CIRCT_CHECKS, CORE_CHECKS, HOST_CHECKS, NATIVE_SUITES, SIMULATOR_CONFIGS, SINGLE_CORE_SOCS, native_configs, simulation_entry, BACKEND_SMOKE_CONFIGS, DEFAULT_BACKEND, BACKEND_SMOKE_VARIANTS, arch_configs, arch_shards, check_matrix, platform_configs, SHORT_PROGRAM_SUITES
from .policy import simulator_options, linux_configs
from .software import ADAPTERS, affected


def matches(path, *patterns):
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


@dataclass
class Selection:
    checks: set[str] = field(default_factory=set)
    programs: set[tuple[str, str]] = field(default_factory=set)
    platforms: set[str] = field(default_factory=set)
    harness_tests: set[str] = field(default_factory=set)
    harness_requests: dict[str, set[str]] = field(default_factory=dict)
    adapters: set[str] = field(default_factory=set)
    software_reasons: dict[str, list[str]] = field(default_factory=dict)
    full_harness: bool = False
    build_all_simulators: bool = False
    arch: bool = False
    linux: bool = False

    def add_checks(self, *keys):
        self.checks.update(keys)
        if "circt-verilog-differential" in keys:
            # Backend comparisons retain HDL golden and integration coverage.
            self.checks.update(("circt-language", "circt-std", "circt-protocols"))

    def add_native(self, *suites):
        for suite in suites:
            self.programs.update((soc, suite) for soc in native_configs(suite))
            if suite == 'coremark':
                self.programs.update((soc, 'coremark_scalar') for soc in native_configs(suite))

    def all_programs(self):
        self.add_native(*NATIVE_SUITES)
        self.arch = True

    def all(self):
        self.add_checks(*(check.key for check in CHECKS))
        self.full_harness = True
        self.linux = True
        self.adapters.update(ADAPTERS)
        self.all_programs()

    def software(self, path):
        """Select declared software consumers without turning their DUTs into harness requests."""
        prefix = 'sims/program-test/performance-baselines/'
        if path.startswith(prefix):
            parts = path[len(prefix):].split('/')
            if len(parts) != 2 or not parts[1].endswith('.json'):
                return False
            soc, suite = parts[0], parts[1][:-5]
            family = 'coremark' if suite == 'coremark_scalar' else suite
            if family in NATIVE_SUITES and soc in native_configs(family):
                self.programs.add((soc, suite))
            elif suite == 'benchmark' and any(key == soc and shape == 'tiled8core'
                                              for key, shape, _core in SIMULATOR_CONFIGS):
                self.harness_requests.setdefault(soc, set()).add('tiled-mt-benchmark-test')
            else:
                raise ValueError(f'performance baseline has no enrolled workload: {path}')
            self.software_reasons[path] = [f'expectation:{soc}/{suite}']
            return True
        consumers = affected(path)
        if not consumers:
            return False
        self.software_reasons[path] = sorted(consumers)
        for consumer in consumers:
            kind, _, name = consumer.partition(':')
            if kind == 'native':
                self.add_native(name)
            elif kind == 'platform':
                self.platforms.add(name)
            elif kind == 'harness':
                self.harness_tests.add({'isa': 'isa-smoke', 'benchmark': 'tiled-mt-benchmark-test'}[name])
            elif kind == 'adapter':
                self.adapters.add(name)
            elif kind == 'image':
                self.linux = True
            elif kind == 'arch':
                self.arch = True
            elif kind == 'simulator':
                self.build_all_simulators = True
        return True

    def classify(self, path):
        documentation = matches(path, "*.md", "LICENSE", "LICENSE.*", "NOTICE", "DCO", "AGENTS.md", ".gitignore", ".gitattributes")
        # Software ownership has one graph. Shared non-software inputs also keep
        # their host/RTL validation below; owned software inputs stop here.
        if not documentation and self.software(path) and matches(path, 'sw/*', 'sims/program-test/*',
                                                                 'sims/opensbi/*', 'sims/linux/*'):
            return
        if not documentation and matches(path, "sims/Makefile", "sims/test-configs.txt",
                                          "socs/*", "cores/*", "riscv/*", "rhodium/*", "flow/*",
                                          "devices/*", "devicetree/*", "support/*",
                                          "tools/run-racket.sh", "tools/racket-build-cache.sh"):
            self.linux = True

        # Native workloads and ACT cover selected single-core configs independently
        # of host, example, and CIRCT checks.
        if documentation or matches(path, "tools/emacs/*"):
            pass
        elif matches(path, "sims/arch-test/*", "sims/tests/test_arch_test.py", "sims/sail/*", "sims/tests/test_sail_config.py", "sw/riscv-arch-test", "sw/riscv-arch-test/*", "sw/riscv-arch-test-patches/*", "riscv/sail-riscv", "riscv/sail-riscv/*", "riscv/sail-riscv-patches/*", "tools/write-riscv-udb-config.rhm"):
            self.arch = True
        elif matches(path, "riscv/patched_submodule.py", "riscv/riscv-isa-sim", "riscv/riscv-isa-sim/*", "riscv/riscv-isa-sim-patches/*"):
            self.all_programs()
        elif matches(path, "rhodium/core/*", "rhodium/lowering/*", "rhodium/frontend/*", "rhodium/base/*", "rhodium/std/*", "rhodium/backend/*", "rhodium/compile/*", "rhodium/language.rhm", "rhodium/main.rkt", "flow/*", "cores/*", "riscv/*", "hardfloat/*", "chi/*", "noc/*", "devices/*", "socs/*", "sims/*", "support/annotations.rhm", "devicetree/*", "tools/install-circt.sh", "tools/install-riscv-*.sh", ".github/actions/setup-riscv-toolchain/*"):
            self.all_programs()

        # Backend oracles cover memory lowering, UART, and Flow events.
        # Their dependencies must select both emission routes.
        if not documentation and matches(path, "rhodium/std/*", "devices/uart/uart.rhdl",
                                         "devices/uart/uart-dpi.rhdl", "devices/uart/dpi/*",
                                         "devices/tests/uart-dpi*", "devices/tests/circt/verilog/uart-dpi*",
                                         "examples/std/sync-ram.rhdl", "flow/*", "rhodium/event/*", "rheg/*"):
            self.add_checks("verilog-direct", "circt-verilog-differential")

        # Rsim behavior follows component ownership, independently of HDL goldens.
        if not documentation and matches(path, "tools/testing/rsim/*", "rhodium/backend/*", "rhodium/compile/*",
                   "rhodium/std/*", "flow/*", "tools/run-racket.sh", "tools/racket-build-cache.sh"):
            self.add_checks(*RSIM_COMPONENT_CHECKS)
        elif not documentation and matches(path, "examples/std/*", "examples/rtl/*", "examples/clocking/*",
                                                "rhodium/event/*", "rhodium/diagram/*", "rheg/*"):
            self.add_checks("rsim-std")
        elif not documentation and matches(path, "chi/*", "noc/*", "devices/*", "socs/*", "examples/chi/*", "examples/noc/*"):
            self.add_checks("rsim-protocols")
        elif not documentation and matches(path, "cores/*", "riscv/*", "hardfloat/*", "examples/cores/*", "examples/riscv/*"):
            self.add_checks(*RSIM_CORE_CHECKS)

        # Trace compilation and collection are shared by all instrumented rsim owners.
        if not documentation and matches(path, "rhodium/event/*", "rhodium/diagram/*", "rheg/*"):
            self.add_checks("rsim-protocols", "rsim-core-components", "rsim-core-cache",
                            "rsim-core-execution-frontend", "rsim-core-execution-datapath",
                            "rsim-core-vector-functional-1")
        # Protocol workloads also supply the trace variants' public-transfer oracles.
        if not documentation and matches(path, "chi/*", "sims/tests/rsim/*", "sims/tests/fesvr-mmio-fixture.rhdl"):
            self.add_checks("rsim-protocols")

        # Shared cache drivers and hardware depend on CHI transaction machinery.
        if not documentation and matches(path, "chi/*"):
            self.add_checks("rsim-core-components", "rsim-core-memory", "rsim-core-cache", "rsim-core-execution-frontend")

        # The rsim smoke also reuses the production UART and PTY helpers.
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
            self.full_harness = True
        elif matches(path, "tools/emacs/*", "vlsi/*"):
            return
        elif matches(path, ".github/workflows/*", ".github/actions/*", "tools/ci/*"):
            self.all()
        elif matches(path, "sims/arch-test/*.rhm", "sims/tests/config-test.rhm"):
            self.add_checks("host-socs")
        elif matches(path, "sims/arch-test/*", "sims/tests/test_arch_test.py", "sims/sail/*", "sims/tests/test_sail_config.py"):
            pass
        elif matches(path, "tools/install-riscv-*.sh"):
            self.full_harness = True
        elif matches(path, "sw/riscv-arch-test", "sw/riscv-arch-test/*", "sw/riscv-arch-test-patches/*", "riscv/sail-riscv", "riscv/sail-riscv/*", "riscv/sail-riscv-patches/*", "sims/arch-test/*", "sims/tests/test_arch_test.py", "sims/sail/*", "sims/tests/test_sail_config.py"):
            pass
        elif matches(path, "riscv/patched_submodule.py"):
            self.add_checks("host-models", "host-backend", "host-hygiene")
            self.full_harness = True
        elif matches(path, "riscv/riscv-isa-sim", "riscv/riscv-isa-sim/*", "riscv/riscv-isa-sim-patches/*"):
            self.add_checks("host-backend", "host-hygiene")
            self.full_harness = True
        elif path == "Makefile":
            self.all()
        elif path == "tools/check-example-verilog.sh":
            self.add_checks("host-hygiene", "host-examples", *CIRCT_CHECKS)
        elif matches(path, "tools/testing/rsim/*"):
            self.add_checks("host-hygiene", *RSIM_COMPONENT_CHECKS)
        elif matches(path, "tools/testing/circt/*"):
            self.add_checks("host-hygiene", "host-examples", *CIRCT_CHECKS)
        elif path == "tools/testing/run-negative.rkt":
            self.add_checks(*HOST_CHECKS)
        elif path == "tools/run-racket-tests.sh":
            self.add_checks(*HOST_CHECKS, "verilog-direct")
            self.full_harness = True
            self.all_programs()
        elif matches(path, "tools/run-racket.sh", "tools/racket-build-cache.sh", "tools/refresh-racket-project-cache.sh", "tools/invalidate-racket-build-cache.rkt"):
            self.add_checks(*HOST_CHECKS, "verilog-direct", *CIRCT_CHECKS)
            self.full_harness = True
            self.all_programs()
        elif path == "tools/write-rv5stage-core-diagram.rhm":
            self.add_checks("host-examples")
        elif path == "tools/write-riscv-udb-config.rhm":
            self.add_checks("host-models", "host-cores", "host-socs")
        elif path == "tools/write-noc-router-diagram.rhm":
            self.add_checks("host-examples")
        elif matches(path, ".githooks/pre-commit", "tools/check-license-headers.sh", "tools/check-parameter-annotations.rkt", "tools/parameter-annotation-scope.txt", "tools/check-boundaries.sh", "rfpl/check-boundaries.sh", "noc/check-boundaries.sh", "riscv/check-boundaries.sh", "chi/check-boundaries.sh", "cores/check-boundaries.sh", "socs/check-boundaries.sh"):
            self.add_checks("host-hygiene")
        elif matches(path, "rhodium/statistics/*", "sims/statistics/*"):
            self.add_checks("host-foundation", "rsim-std", "circt-verilog-differential")
            self.full_harness = True
        elif matches(path, "rhodium/event/*", "rheg/*"):
            self.add_checks("host-foundation", "host-backend", "circt-language")
            self.full_harness = True
        elif matches(path, "rhodium/core/*", "rhodium/lowering/*", "rhodium/analysis/*", "rhodium/frontend/*", "rhodium/base/*", "rhodium/language.rhm", "rhodium/main.rkt"):
            self.all()
        elif matches(path, "rhodium/std/*", "flow/*"):
            self.add_checks("host-hygiene", "host-foundation", "host-backend", "host-protocols", "host-cores", "host-socs", "host-examples", "circt-language", "circt-std", "circt-protocols", *CORE_CHECKS)
            self.full_harness = True
        elif matches(path, "rhodium/backend/*", "rhodium/compile/*"):
            self.add_checks("host-backend", "verilog-direct", *CIRCT_CHECKS)
            self.full_harness = True
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
            self.add_checks("host-models", "host-cores", "host-socs", "host-examples", *CORE_CHECKS)
        elif matches(path, "hardfloat/*"):
            self.add_checks("host-models", *CORE_CHECKS)
            self.full_harness = True
        elif matches(path, "devicetree/*"):
            self.add_checks("host-models")
        elif matches(path, "chi/subordinate/memory-controller.rhdl", "chi/subordinate/dpi-memory.rhdl", "chi/subordinate/dpi/*", "chi/tests/dpi-memory-*", "chi/tests/chi_dpi_memory_*"):
            self.add_checks("host-protocols", "host-socs", "circt-protocols", "host-examples")
            self.full_harness = True
        elif matches(path, "chi/*"):
            self.add_checks("host-protocols", "host-socs", "circt-protocols", "host-examples")
        elif matches(path, "cores/*"):
            self.add_checks("host-cores", "host-socs", "host-examples", *CORE_CHECKS)
            self.full_harness = True
        elif matches(path, "sims/fesvr/*.rhdl", "sims/tests/rsim/*", "sims/tests/fesvr-mmio-fixture.rhdl"):
            self.add_checks("rsim-protocols", "circt-protocols")
            self.full_harness = True
        elif matches(path, "sims/cosim/*"):
            self.add_checks("host-socs", "circt-core-components", "circt-core-execution-datapath")
            self.full_harness = True
        elif matches(path, "sims/*"):
            self.full_harness = True
        elif matches(path, "socs/*"):
            self.add_checks("host-socs", *CORE_CHECKS)
            self.full_harness = True
        elif matches(path, "support/annotations.rhm", "support/tests/*"):
            self.add_checks("host-foundation")
        elif path == "support/README.md":
            self.add_checks("host-foundation")
        elif path == "tools/install-circt.sh":
            self.add_checks(*CIRCT_CHECKS)
            self.full_harness = True
        else:
            self.all()

    def result(self):
        matrix = check_matrix(self.checks)
        suites = {'include': [dict(soc=soc, suite=suite, **simulator_options(soc), backend=DEFAULT_BACKEND)
                              for soc, suite in sorted(self.programs)]}
        platforms = {'include': [row for row in platform_configs()
                                 if self.full_harness or row['suite'] in self.platforms]}
        arch_builds = {row['configuration']: row for row in arch_configs()} if self.arch else {}
        arch_runs = arch_shards() if self.arch else ()
        needed = ({row['soc'] for row in suites['include'] + platforms['include']} | set(arch_builds))
        builds = []
        configs = [(*config, DEFAULT_BACKEND) for config in SIMULATOR_CONFIGS]
        configs.extend((*config, backend) for config in BACKEND_SMOKE_CONFIGS for backend in BACKEND_SMOKE_VARIANTS)
        for soc, shape, core, backend in configs:
            row = simulation_entry(soc, shape, core, backend, smoke_variant=backend != DEFAULT_BACKEND)
            available = row['software_tests'].split()
            requested = self.harness_tests | self.harness_requests.get(soc, set())
            tests = available if self.full_harness else [test for test in available if test in requested]
            row.update(software_tests=' '.join(tests), run_harness=bool(tests))
            if tests or self.build_all_simulators or (backend == DEFAULT_BACKEND and soc in needed):
                builds.append(row)
        run_checks = bool(self.checks)
        run_program_native = bool(self.programs)
        run_simulator = bool(builds)
        linux = {'include': linux_configs() if self.linux else []}
        run_targets = run_simulator or self.linux
        harness_build_suites = sorted({{'isa-smoke': 'isa', 'tiled-mt-benchmark-test': 'benchmark'}[test]
                                      for row in builds for test in row['software_tests'].split()
                                      if test in ('isa-smoke', 'tiled-mt-benchmark-test')})
        adapters = set(ADAPTERS) if self.full_harness else self.adapters
        # Matching-row needs are not supported by Actions matrices. Configs
        # with downstream execution use named jobs; build-only rows stay batched.
        pipelines = (set(SINGLE_CORE_SOCS) | {row['soc'] for row in platform_configs()}
                     | {soc for soc, shape, _core in SIMULATOR_CONFIGS if shape == 'tiled8core'})
        config_jobs = {}
        groups = {key: {'include': []} for key in ('plain', 'cosim', 'plain-elf', 'cosim-elf')}
        for build in builds:
            if build['backend'] == DEFAULT_BACKEND and build['soc'] in pipelines:
                soc = build['soc']
                config_jobs[soc] = dict(simulator=build,
                                       program_groups=list(dict.fromkeys('short' if row['suite'] in SHORT_PROGRAM_SUITES else row['suite']
                                                                         for row in suites['include'] if row['soc'] == soc)),
                                       arch=soc in arch_builds,
                                       arch_config=arch_builds.get(soc),
                                       arch_matrix={'include': [row for row in arch_runs if row['configuration'] == soc]},
                                       platform=any(row['soc'] == soc for row in platforms['include']))
            else:
                shared_elf = any(target in build.get('software_tests', '').split()
                                 for target in ('isa-smoke', 'tiled-mt-benchmark-test'))
                key = ('cosim' if build['cosim'] else 'plain') + ('-elf' if shared_elf else '')
                groups[key]['include'].append(build)
        return {
            "run_compile": run_checks or run_targets,
            "run_sail": self.arch or any(build["cosim"] for build in builds),
            "run_cosim": any(build["cosim"] for build in builds),
            "run_native": run_simulator,
            "software_checks": sorted(ADAPTERS[name][0] for name in adapters),
            "software_setup": sorted({setup for name in adapters for setup in ADAPTERS[name][1]}),
            "software_reasons": self.software_reasons,
            "run_spike_tests": any(build["core"] == "spike" for build in builds),
            "run_checks": run_checks,
            "checks_matrix": matrix,
            "run_simulator": run_simulator,
            "run_targets": run_targets,
            "target_configs": sorted({row['soc'] for row in builds + linux['include']}),
            "run_linux_build": self.linux,
            "linux_matrix": linux,
            "simulator_matrix": {"include": builds},
            "harness_build_suites": harness_build_suites,
            "platform_matrix": platforms,
            "platform_build_suites": sorted({row['suite'] for row in platforms['include']}),
            "run_program_native": run_program_native,
            "program_matrix": suites,
            "run_program_arch": self.arch,
            "config_jobs": config_jobs,
            "simulator_groups": groups,
            "program_build_groups": list(dict.fromkeys('short' if row['suite'] in SHORT_PROGRAM_SUITES and row['suite'] != 'isa' else row['suite']
                                                       for row in suites['include'])),
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
