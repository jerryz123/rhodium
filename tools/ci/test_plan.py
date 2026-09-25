# Tests the declarative CI policy, fail-closed coverage, and stable matrix shape.
# SPDX-License-Identifier: Apache-2.0

import subprocess
import unittest
from pathlib import Path

from .gate import failures
from .plan import Selection, plan_for_paths
from .policy import CHECKS, NATIVE_SUITES, SIMULATOR_PRODUCTS, SINGLE_CORE_SOCS, SOFTWARE_TESTS, simulation_entry


REPO = Path(__file__).resolve().parents[2]


def check_keys(plan):
    return {entry["key"] for entry in plan["checks_matrix"]["include"]}


def suites(plan):
    selected = []
    for entry in plan["program_matrix"]["include"]:
        suite = "coremark" if entry["suite"] == "coremark_scalar" else entry["suite"]
        if suite not in selected:
            selected.append(suite)
    return selected


def program_entries(plan):
    return [(entry["soc"], entry["suite"]) for entry in plan["program_matrix"]["include"]]


class PlanTest(unittest.TestCase):
    def plan(self, *paths):
        return plan_for_paths(paths)

    def assert_checks(self, path, *expected):
        self.assertTrue(set(expected).issubset(check_keys(self.plan(path))), path)

    def test_documentation_selects_no_execution(self):
        for path in ("README.md", "LICENSE", "NOTICE", "DCO", "flow/DEVELOPING.md", "sram/README.md"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertFalse(plan["run_compile"])
                self.assertFalse(plan["run_simulator"])

    def test_workload_sources_select_the_expected_suites(self):
        cases = {
            "sw/build/isa.mk": (["isa"], False),
            "sw/build/build.py": (["isa", "benchmark"], False),
            "sw/build/build-coremark.py": (["coremark"], False),
            "sw/build/build-embench.py": (["embench"], False),
            "sw/build/build-bringup-bench.py": (["bringup"], False),
            "sims/arch-test/configure.py": ([], True),
        }
        for path, (native, arch) in cases.items():
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertEqual(suites(plan), native)
                self.assertEqual(plan["run_program_arch"], arch)

    def test_native_matrix_covers_both_products_and_coremark_variants(self):
        plan = self.plan("sw/build/build-coremark.py")
        expected = [(soc, suite) for suite in ("coremark", "coremark_scalar") for soc in SINGLE_CORE_SOCS]
        self.assertEqual(program_entries(plan), expected)

    def test_simulation_builds_and_runs_all_qualified_products(self):
        plan = self.plan("sims/Makefile")
        expected = [dict(soc=soc, shape=shape, core=core)
                    for soc, shape, core in SIMULATOR_PRODUCTS]
        self.assertEqual(plan["simulator_matrix"]["include"], expected)
        self.assertEqual(plan["simulation_matrix"]["include"],
                         [simulation_entry(*product) for product in SIMULATOR_PRODUCTS])
        self.assertEqual({entry["soc"] for entry in expected},
                         {"mini-rv5stage-rv32max", "mini-spike-rv32max", "mini-rv5stage-rva23", "mini-spike-rva23", "simple-rv5stage-rva23",
                          "simple-spike-rva23", "tiled-rv5stage-rva23", "tiled-spike-rva23"})
        for path in ("sw/build/build.py", "sw/build/isa.mk", "sw/riscv-isa-tests"):
            with self.subTest(path=path):
                self.assertIn(simulation_entry("mini-rv5stage-rv32max", "mini", "rv5stage"),
                              self.plan(path)["simulation_matrix"]["include"])

    def test_software_selection_is_identical_for_matching_shape_and_isa(self):
        entries = self.plan("sims/Makefile")["simulation_matrix"]["include"]
        for (shape, isa), tests in SOFTWARE_TESTS.items():
            products = [entry for entry in entries if (entry["shape"], entry["isa"]) == (shape, isa)]
            self.assertEqual({entry["core"] for entry in products}, {"rv5stage", "spike"})
            self.assertEqual({entry["software_tests"] for entry in products}, {" ".join(tests)})

    def test_software_only_builds_only_existing_single_core_products(self):
        for path in ("sw/build/build-coremark.py", "sims/arch-test/configure.py"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertEqual([entry["soc"] for entry in plan["simulator_matrix"]["include"]],
                                 list(SINGLE_CORE_SOCS))
                self.assertEqual(plan["simulation_matrix"]["include"], [])
                self.assertFalse(plan["run_simulation"])

    def test_shared_program_build_inputs_select_every_suite(self):
        for path in ("sw/build/program_target.py", "sw/tests/test_program_build.py"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertEqual(suites(plan), list(NATIVE_SUITES))
                self.assertTrue(plan["run_program_arch"])

    def test_opensbi_sources_select_simulation_without_program_matrices(self):
        for path in ("sw/build/opensbi.py", "sw/tests/test_opensbi_build.py", "sw/opensbi"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertTrue(plan["run_simulation"])
                self.assertFalse(plan["run_program_native"])
                self.assertFalse(plan["run_program_arch"])

    def test_litmus_sources_select_simulation_without_single_core_programs(self):
        for path in ("sw/build/build-litmus.py", "sw/litmus-riscv-baremetal/smoke-cases.txt",
                     "sw/litmus-tests-riscv", "sw/tests/test_litmus_build.py"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertTrue(plan["run_simulation"])
                self.assertFalse(plan["run_program_native"])
                self.assertFalse(plan["run_program_arch"])

    def test_hardware_changes_select_every_software_suite(self):
        for path in ("cores/rv5stage/core.rhdl", "chi/protocol/link.rhdl", "noc/rtl/router.rhdl", "devices/aclint.rhdl", "socs/single-core-rv5stage-soc.rhdl", "sims/TestDriver.v", "rhodium/backend/circt.rhm"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertEqual(suites(plan), list(NATIVE_SUITES))
                self.assertTrue(plan["run_program_arch"])

    def test_representative_dependency_edges(self):
        cases = {
            "rhodium/core/ir.rhm": ("host-foundation", "circt-language"),
            "rhodium/event/instrument.rhm": ("host-foundation", "host-backend", "circt-language"),
            "flow/queue.rhdl": ("host-foundation", "host-backend", "host-protocols", "host-cores", "host-socs", "host-hygiene", "circt-std", "circt-protocols", "circt-core-cache", "example-std"),
            "rhodium/backend/tests/circt/verilog/adder_tb.sv": ("host-backend", "circt-language", "circt-rfpl"),
            "devicetree/main.rhm": ("host-models", "host-hygiene"),
            "noc/rtl/router.rhdl": ("host-models", "host-socs", "circt-protocols", "example-noc"),
            "hardfloat/rtl/recode.rhdl": ("host-models", "circt-core-cache", "circt-hardfloat"),
            "chi/subordinate/dpi-memory.rhdl": ("host-protocols", "host-socs", "circt-protocols", "example-chi"),
            "cores/rv5stage/core.rhdl": ("host-cores", "host-socs", "circt-core-execution", "example-cores", "example-rv5stage"),
            "socs/mini-rv5stage-soc.rhdl": ("host-socs", "circt-core-memory", "host-hygiene"),
            "examples/rfpl/circuit-pair.rhdl": ("example-rfpl", "circt-rfpl", "host-hygiene"),
            "tools/write-riscv-udb-config.rhm": ("host-models", "host-cores", "host-socs", "host-hygiene"),
        }
        for path, expected in cases.items():
            with self.subTest(path=path):
                self.assert_checks(path, *expected)

    def test_flow_and_standard_library_share_the_same_plan(self):
        self.assertEqual(self.plan("flow/queue.rhdl"), self.plan("rhodium/std/ready-valid.rhdl"))

    def test_simulation_only_paths_do_not_expand_host_checks(self):
        for path in ("sram/map-memories.py", "vlsi/sim/Makefile", "vlsi/designs/mini-rv5stage-soc/sky130/sram-map.yaml"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertTrue(plan["run_simulation"])
                self.assertFalse(plan["run_checks"])

    def test_optional_sources_remain_outside_functional_ci(self):
        self.assertFalse(self.plan("tools/emacs/rhodium-mode.el")["run_compile"])
        self.assert_checks("vlsi/src/rhodium-top.rhdl", "host-hygiene")

    def test_unknown_and_ci_infrastructure_fail_closed(self):
        all_keys = {check.key for check in CHECKS}
        for path in ("unrecognized/new-tool.py", ".github/workflows/ci.yml", ".github/actions/setup-circt/action.yml", "tools/ci/plan.py"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertEqual(check_keys(plan), all_keys)
                self.assertTrue(plan["run_simulation"])
                self.assertEqual(suites(plan), list(NATIVE_SUITES))
                self.assertTrue(plan["run_program_arch"])

    def test_all_is_complete_and_stably_ordered(self):
        selection = Selection()
        selection.all()
        plan = selection.result()
        self.assertEqual([entry["key"] for entry in plan["checks_matrix"]["include"]], [check.key for check in CHECKS])
        self.assertEqual(suites(plan), list(NATIVE_SUITES))
        self.assertTrue(plan["run_simulation"])
        self.assertTrue(plan["run_program_arch"])

    def test_check_declarations_are_unique_and_self_consistent(self):
        self.assertEqual(len({check.key for check in CHECKS}), len(CHECKS))
        self.assertEqual(len({check.target for check in CHECKS}), len(CHECKS))
        for check in CHECKS:
            with self.subTest(check=check.key):
                self.assertTrue(check.name)
                self.assertGreater(check.timeout, 0)
                if check.circt:
                    self.assertTrue(check.verilator)

    def test_vector_functional_shards_are_nonempty_disjoint_and_exhaustive(self):
        runner = REPO / "tools/testing/circt/run.sh"

        def fixtures(group):
            output = subprocess.run(["bash", runner, "--group", group, "--list-fixtures"], cwd=REPO, check=True, text=True, capture_output=True).stdout
            return set(output.splitlines())

        first = fixtures("cores-vector-functional-1")
        second = fixtures("cores-vector-functional-2")
        combined = fixtures("cores-vector-functional")
        expected = {
            "rv5stage-vector", "event-vector", "rv5stage-vector-control", "rv5stage-vector-config",
            "rv5stage-vector-fp", "rv5stage-vector-muldiv", "rv5stage-vector-reduction",
            "rv5stage-vector-memory", "rv5stage-vector-packed", "rv5stage-vector-overlap",
            "rv5stage-vector-admission", "rv5stage-vector-sequencer", "rv5stage-zvkt",
        }
        self.assertTrue(first)
        self.assertTrue(second)
        self.assertTrue(first.isdisjoint(second))
        self.assertEqual(first | second, expected)
        self.assertEqual(combined, expected)

    def test_every_tracked_executable_input_selects_a_lane(self):
        tracked = subprocess.run(["git", "ls-files"], cwd=REPO, check=True, text=True, capture_output=True).stdout.splitlines()
        suffixes = (".mk", ".inc", ".py", ".rhm", ".rhdl", ".rkt", ".rktd", ".sh", ".sv", ".cc", ".cpp", ".h", ".S", ".ld", ".rfpl", ".yml", ".yaml")
        for path in tracked:
            if path.startswith("tools/emacs/"):
                continue
            if path.startswith("vlsi/") and not path.startswith(("vlsi/sim/", "vlsi/designs/mini-rv5stage-soc/sky130/")):
                continue
            if path == "Makefile" or path.endswith(suffixes):
                with self.subTest(path=path):
                    plan = self.plan(path)
                    self.assertTrue(plan["run_checks"] or plan["run_simulator"], path)

    def test_gate_requires_exactly_the_selected_workflows(self):
        plan = self.plan("examples/rtl/alu.rhdl")
        results = {"compile": "success", "checks": "success", "simulator": "skipped", "simulation": "skipped", "software": "skipped"}
        self.assertEqual(failures(plan, results), [])
        results["checks"] = "failure"
        self.assertEqual(failures(plan, results), ["selected job checks finished with failure"])

        plan = self.plan("README.md")
        results = {name: "skipped" for name in ("compile", "checks", "simulator", "simulation", "software")}
        self.assertEqual(failures(plan, results), [])

    def test_workflow_orchestration_contract(self):
        root = (REPO / ".github/workflows/ci.yml").read_text()
        self.assertIn("group: ci-${{ github.workflow }}-${{ github.event.pull_request.number || github.run_id }}", root)
        self.assertIn("cancel-in-progress: ${{ github.event_name == 'pull_request' }}", root)
        self.assertIn("name: CI\n    needs: [plan, compile-racket, checks, simulator, simulation, software]", root)
        for workflow in ("ci-checks.yml", "ci-simulator.yml", "ci-simulation.yml", "ci-software.yml"):
            with self.subTest(workflow=workflow):
                text = (REPO / ".github/workflows" / workflow).read_text()
                self.assertIn("workflow_call:", text)
                self.assertIn("# SPDX-License-Identifier: Apache-2.0", text)
                self.assertIn(f"uses: ./.github/workflows/{workflow}", root)
        self.assertNotIn("ci-changes.sh", root)

    def test_precompiled_racket_versions_reach_test_steps(self):
        action = (REPO / ".github/actions/setup-racket-artifact/action.yml").read_text()
        verify_step = action.split("    - name: Verify compiled root\n", 1)[1]
        for variable, input_name in (("RACKET_VERSION", "racket-version"), ("RHOMBUS_CHECKSUM", "rhombus-checksum")):
            with self.subTest(variable=variable):
                self.assertIn(f"{variable}: ${{{{ inputs.{input_name} }}}}", verify_step)
                self.assertIn(f'echo "{variable}=${variable}" >> "$GITHUB_ENV"', verify_step)
        self.assertIn("tools/racket-artifact.sh verify", verify_step)
        self.assertIn('echo "RHODIUM_PRECOMPILED=1" >> "$GITHUB_ENV"', verify_step)

    def test_product_workflows_follow_shape_policy(self):
        root = (REPO / ".github/workflows/ci.yml").read_text()
        build = (REPO / ".github/workflows/ci-simulator.yml").read_text()
        simulation = (REPO / ".github/workflows/ci-simulation.yml").read_text()
        software = (REPO / ".github/workflows/ci-software.yml").read_text()
        self.assertIn(".simulator_matrix", root)
        self.assertIn(".simulation_matrix", root)
        self.assertIn("matrix: ${{ fromJSON(inputs.matrix) }}", build)
        self.assertIn("matrix: ${{ fromJSON(inputs.matrix) }}", simulation)
        self.assertIn("name: ${{ matrix.soc }}-${{ github.sha }}", build)
        self.assertIn("name: ${{ matrix.soc }}-${{ github.sha }}", simulation)
        self.assertIn("SOFTWARE_TESTS: ${{ matrix.software_tests }}", simulation)
        self.assertIn('for target in $SOFTWARE_TESTS', simulation)
        self.assertIn('make -C sims "$target" SOC="$SOC"', simulation)
        self.assertIn("if: matrix.shape != 'single'", simulation)
        self.assertIn('tiled-mt-benchmark-test', SOFTWARE_TESTS['tiled', 'rva23'])
        self.assertIn("tiled-litmus-smoke:", simulation)
        self.assertIn("{soc: tiled-rv5stage-rva23, core: rv5stage}", simulation)
        self.assertIn("{soc: tiled-spike-rva23, core: spike}", simulation)
        self.assertIn("litmus-smoke-test", simulation)
        self.assertNotIn("litmus-full", simulation)
        self.assertIn("if: matrix.soc == 'simple-rv5stage-rva23'", simulation)
        self.assertEqual(software.count("configuration: [simple-rv5stage-rva23, simple-spike-rva23]"), 2)
        self.assertIn("Restore pinned Spike runtime libraries", software)


if __name__ == "__main__":
    unittest.main()
