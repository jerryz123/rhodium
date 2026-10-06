# Tests CI selection, fail-closed coverage, and completed build-artifact orchestration.
# SPDX-License-Identifier: Apache-2.0

import json
import os
import re
import tempfile
import textwrap
import shlex
import subprocess
import unittest
from pathlib import Path

from .gate import failures
from .plan import Selection, plan_for_paths
from .programs import program_matrices
from .policy import CHECKS, NATIVE_SUITES, SIMULATOR_CONFIGS, SINGLE_CORE_SOCS, SOFTWARE_TESTS, native_configs, simulation_entry, simulator_entry, BACKEND_SMOKE_CONFIG, BACKEND_SMOKE_VARIANTS, platform_configs


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

    def test_direct_verilog_routes_have_explicit_tool_requirements(self):
        plan = self.plan("rhodium/backend/verilog.rhm")
        entries = {entry["key"]: entry for entry in plan["checks_matrix"]["include"]}
        self.assertFalse(entries["verilog-direct"]["circt"])
        self.assertTrue(entries["verilog-direct"]["verilator"])
        self.assertTrue(entries["circt-verilog-differential"]["circt"])
        self.assert_checks("rhodium/core/ops.rhm", "verilog-direct")
        for path in ("rhodium/backend/tests/verilog/run.py", "rhodium/compile/rtl.rhm"):
            self.assert_checks(path, "host-backend", "verilog-direct", "circt-verilog-differential")

    def test_authored_backend_integration_dependencies(self):
        for path in ("rhodium/backend/tests/verilog/emit-integration.rhm",
                     "rhodium/backend/tests/verilog/run-integration.py",
                     "rhodium/event/trace-pass.rhm", "flow/event.rhdl", "rheg/runtime/rheg.cc",
                     "rhodium/event/tests/circt/verilog/event-runtime_tb.sv",
                     "rhodium/event/tests/circt/verilog/event-elastic_dpi.cpp",
                     "rhodium/std/sync-ram.rhdl", "rhodium/std/ready-valid.rhdl",
                     "rhodium/std/tests/circt/verilog/sync-ram_tb.sv",
                     "examples/std/sync-ram.rhdl", "devices/uart/uart-dpi.rhdl",
                     "devices/uart/dpi/uart_dpi.cc", "devices/uart/dpi/uart_dpi.h",
                     "devices/tests/uart-dpi-fixture.rhdl",
                     "devices/tests/circt/verilog/uart-dpi_tb.sv",
                     "devices/tests/circt/verilog/uart-dpi_dpi.cpp"):
            with self.subTest(path=path):
                self.assert_checks(path, "verilog-direct", "circt-verilog-differential")

    def test_authored_comparisons_enroll_the_reused_circt_behavioral_owners(self):
        for path in ("rhodium/backend/tests/verilog/run-integration.py", "examples/std/sync-ram.rhdl",
                     "devices/uart/dpi/uart_dpi.cc", "rhodium/event/trace-pass.rhm"):
            self.assert_checks(path, "circt-verilog-differential", "circt-language", "circt-std", "circt-protocols")

    def test_example_execution_is_partitioned_between_host_and_circt(self):
        self.assert_checks("tools/testing/circt/run.sh", "host-hygiene", "host-examples", "circt-language")
        circt = set(subprocess.run(["bash", "tools/testing/circt/run.sh", "--list-example-sources"],
                                  cwd=REPO, check=True, text=True, capture_output=True).stdout.splitlines())
        host = set(subprocess.run(["make", "--no-print-directory", "-s", "print-ci-host-examples"],
                                 cwd=REPO, check=True, text=True, capture_output=True).stdout.splitlines())
        examples = {str(path.relative_to(REPO)) for path in (REPO / "examples").rglob("*")
                    if path.suffix in (".rhdl", ".rhm", ".rfpl")
                    and "formal" not in path.relative_to(REPO / "examples").parts}
        self.assertEqual(host | (circt & examples), examples)
        self.assertTrue(host.isdisjoint(circt))
        self.assertTrue({"examples/clocking/report.rhm", "examples/rv5stage/core-diagram.rhdl"} <= host)
        for path, owner in (("examples/clocking/reconvergence.rhdl", "circt-language"),
                            ("examples/std/flit-formats.rhdl", "circt-std"),
                            ("examples/noc/noc-router.rhdl", "circt-protocols"),
                            ("examples/riscv/instruction-fields.rhdl", "circt-core-components"),
                            ("examples/cores/rv5stage.rhdl", "circt-core-execution-datapath"),
                            ("examples/rfpl/circuit-pair.rhdl", "circt-rfpl")):
            self.assert_checks(path, owner)

    def test_ci_host_batches_run_each_file_once_and_include_library_contracts(self):
        targets = [check.target for check in CHECKS if check.key.startswith("host-")]
        commands = subprocess.run(["make", "--no-print-directory", "-n", *targets],
                                  cwd=REPO, check=True, text=True, capture_output=True).stdout.splitlines()
        files = [path for command in commands if command.startswith("tools/run-racket-tests.sh ")
                 for path in shlex.split(command)[1:]]
        self.assertEqual(len(files), len(set(files)))
        for root in ("rhodium/std/tests", "flow/tests", "rhodium/event/tests", "rhodium/diagram/tests"):
            expected = {str(path.relative_to(REPO)) for path in (REPO / root).glob("*-test.rhm")}
            self.assertTrue(expected)
            self.assertTrue(expected <= set(files), root)

    def test_rsim_uart_integration_dependencies(self):
        for path in ("rhodium/backend/tests/rsim/uart-fixture.rhdl",
                     "rhodium/backend/tests/rsim/emit-uart.rhm",
                     "rhodium/backend/tests/rsim/uart.cpp", "rhodium/backend/tests/rsim/uart.py",
                     "devices/uart/uart.rhdl", "devices/uart/uart-dpi.rhdl",
                     "devices/uart/dpi/uart_dpi.cc", "devices/uart/dpi/uart_dpi.h",
                     "devices/tests/circt/verilog/uart-dpi_dpi.cpp",
                     "rhodium/std/cdc/level.rhdl", "rhodium/std/ready-valid.rhdl"):
            with self.subTest(path=path):
                self.assert_checks(path, "host-backend", "circt-verilog-differential")

    def test_rsim_sv_bridge_dependencies(self):
        for path in ("sims/TestDriver.v", "sims/verilator/simulation_runtime.cc", "sims/verilator/simulation_runtime.h",
                     "rhodium/backend/tests/rsim/emit-sv-bridge.rhm",
                     "rhodium/backend/rsim/sv-binding.rhm", "rhodium/backend/tests/rsim-sv-test.rhm",
                     "rhodium/backend/tests/rsim/sv-binding-fixtures.rhm",
                     "rhodium/backend/tests/rsim/sv-bridge-host.cpp", "rhodium/backend/tests/rsim/sv-bridge-bench.sv",
                     "rhodium/backend/tests/rsim/sv_bridge.py"):
            with self.subTest(path=path):
                self.assert_checks(path, "circt-verilog-differential")

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
            "sims/sail/configuration.py": ([], True),
            "sims/tests/test_sail_config.py": ([], True),
            "riscv/sail-riscv": ([], True),
            "riscv/sail-riscv-patches/0001-mask-sgeie-when-geilen-is-zero.patch": ([], True),
        }
        for path, (native, arch) in cases.items():
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertEqual(suites(plan), native)
                self.assertEqual(plan["run_program_arch"], arch)

    def test_native_matrix_covers_both_configs_and_coremark_variants(self):
        plan = self.plan("sw/build/build-coremark.py")
        expected = [(soc, suite) for suite in ("coremark", "coremark_scalar") for soc in native_configs("coremark")]
        self.assertEqual(program_entries(plan), expected)

    def test_simulation_builds_and_runs_all_configs(self):
        plan = self.plan("sims/Makefile")
        expected = [simulation_entry(*config) for config in SIMULATOR_CONFIGS]
        expected.extend(simulation_entry(*BACKEND_SMOKE_CONFIG, backend=backend)
                        for backend in BACKEND_SMOKE_VARIANTS)
        self.assertEqual(plan["simulator_matrix"]["include"], expected)
        self.assertEqual({entry["soc"] for entry in expected},
                         {"mini-rv5stage-rv32int", "mini-spike-rv32int", "simple-rv5stage-rv32int", "simple-spike-rv32int",
                          "mini-rv5stage-rv32max", "mini-spike-rv32max", "simple-rv5stage-rv64max", "simple-spike-rv64max", "mini-rv5stage-rva23", "mini-spike-rva23", "simple-rv5stage-rva23",
                          "simple-spike-rva23", "simple-rv5stage-rv32max", "simple-spike-rv32max", "tiled-rv5stage-rva23", "tiled-spike-rva23",
                          "simple-rv5stage-rv64imacb", "simple-spike-rv64imacb", "simple-rv5stage-rv64imafdcb", "simple-spike-rv64imafdcb"})
        for path in ("sw/build/build.py", "sw/build/isa.mk", "sw/riscv-isa-tests"):
            with self.subTest(path=path):
                self.assertIn(simulation_entry("mini-rv5stage-rv32max", "mini", "rv5stage"),
                              self.plan(path)["simulator_matrix"]["include"])

    def test_direct_simulator_is_an_isolated_smoke_variant(self):
        for path in ('rhodium/backend/verilog.rhm', 'rhodium/compile/rtl.rhm',
                     'cores/rv5stage/core.rhdl', 'sims/Makefile', '.github/workflows/ci.yml'):
            with self.subTest(path=path):
                plan = self.plan(path)
                builds = plan['simulator_matrix']['include']
                runs = plan['simulator_matrix']['include']
                self.assertEqual(len({entry['simulator_id'] for entry in builds}), len(builds))
                self.assertEqual({entry['simulator_id'] for entry in builds},
                                 {entry['simulator_id'] for entry in runs})
                direct = [entry for entry in runs if entry['backend'] == 'verilog']
                self.assertEqual(len(direct), 1)
                self.assertEqual(direct[0]['soc'], 'simple-rv5stage-rva23')
                self.assertEqual(direct[0]['simulator_id'], 'simple-rv5stage-rva23-verilog')
                self.assertEqual(direct[0]['software_tests'].split(), ['smoke', 'host-mmio-test', 'uart-pty-test'])
                self.assertEqual(len(plan['arch_build_matrix']['include']), 6)
        build = (REPO / '.github/workflows/ci-simulator.yml').read_text()
        run = (REPO / '.github/workflows/ci-harness.yml').read_text()
        self.assertIn("if: fromJSON(inputs.config).backend == 'circt'", build)
        for workflow in (build, run):
            self.assertIn('RTL_BACKEND: ${{ fromJSON(inputs.config).backend }}', workflow)
            self.assertIn('fromJSON(inputs.config).simulator_id', workflow)
        self.assertIn('--backend "$RTL_BACKEND"', run)

    def test_rsim_is_an_isolated_bounded_smoke_variant(self):
        for path in ('rhodium/backend/rsim/emit.rhm', 'rhodium/compile/rtl.rhm',
                     'cores/rv5stage/core.rhdl', 'sims/TestDriver.v', '.github/workflows/ci.yml'):
            with self.subTest(path=path):
                plan = self.plan(path)
                entries = plan['simulator_matrix']['include']
                self.assertEqual(len({entry['simulator_id'] for entry in entries}), len(entries))
                rsim = [entry for entry in entries if entry['backend'] == 'rsim']
                self.assertEqual(len(rsim), 1)
                self.assertEqual(rsim[0]['simulator_id'], 'simple-rv5stage-rva23-rsim')
                self.assertEqual(rsim[0]['soc'], 'simple-rv5stage-rva23')
                self.assertEqual(rsim[0]['software_tests'], 'smoke')
                self.assertTrue(all(row['opt_fast'] == '-O2' for row in entries))
                self.assertEqual(rsim[0]['smoke_max_cycles'], 100000)
                self.assertEqual(rsim[0]['harness_timeout_minutes'], 5)
                self.assertEqual(len(plan['arch_build_matrix']['include']), 6)
                self.assertTrue(all(row['soc'] in SINGLE_CORE_SOCS for row in plan['program_matrix']['include']))
        # Software-only builds still use the ordinary simulator inventory.
        self.assertTrue(all(row['backend'] == 'circt' for row in
                            self.plan('sw/build/build-coremark.py')['simulator_matrix']['include']))
        self.assertTrue({row['soc'] for row in platform_configs()} <= {c[0] for c in SIMULATOR_CONFIGS})

    def test_backend_workflow_commands_propagate_settings_and_failures(self):
        def step(workflow, name):
            text = (REPO / '.github/workflows' / workflow).read_text()
            section = text.split(f'      - name: {name}\n', 1)[1].split('\n      - name:', 1)[0]
            self.assertIn('        shell: bash\n', section)
            return section, textwrap.dedent(section.split('        run: |\n', 1)[1])

        build_section, build = step('ci-simulator.yml', 'Build and attest simulator')
        run_section, run = step('ci-harness.yml', 'Run shape-and-ISA software selection')
        self.assertIn('OPT_FAST: ${{ fromJSON(inputs.config).opt_fast }}', build_section)
        self.assertIn('timeout-minutes: ${{ fromJSON(inputs.config).harness_timeout_minutes || 60 }}', run_section)
        self.assertIn('SMOKE_MAX_CYCLES: ${{ fromJSON(inputs.config).smoke_max_cycles }}', run_section)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            calls = root / 'calls.jsonl'
            # Exercise the actual workflow shell, substituting only expensive tools.
            stub = textwrap.dedent("""\
                #!/usr/bin/env python3
                import json
                import os
                import sys
                from pathlib import Path
                with open(os.environ['CALL_LOG'], 'a') as output:
                    output.write(json.dumps([Path(sys.argv[0]).name, *sys.argv[1:]]) + '\\n')
                print('tool transcript')
                sys.exit(int(os.environ.get('MAKE_EXIT', '0')) if Path(sys.argv[0]).name == 'make' else 0)
            """)
            for name in ('make', 'ldd'):
                tool = root / name
                tool.write_text(stub)
                tool.chmod(0o755)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                       CALL_LOG=str(calls), RUNNER_TEMP=str(root), SOC='simple-rv5stage-rva23',
                       RTL_BACKEND='rsim', SIMULATOR_ID='simple-rv5stage-rva23-rsim',
                       SOFTWARE_TESTS='smoke', SMOKE_MAX_CYCLES='100000',
                       COSIM='0', GITHUB_OUTPUT=str(root / 'github-output'))
            for opt in ('-O2', '-O0', ''):
                calls.write_text('')
                result = subprocess.run(['bash', '-eo', 'pipefail', '-c', build], env=dict(env, OPT_FAST=opt),
                                        text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                commands = [json.loads(line) for line in calls.read_text().splitlines()]
                self.assertEqual([c[0] for c in commands], ['make', 'ldd'])
                self.assertEqual([arg for arg in commands[0] if arg.startswith('OPT_FAST=')],
                                 [f'OPT_FAST={opt}'] if opt else [])
            calls.write_text('')
            result = subprocess.run(['bash', '-eo', 'pipefail', '-c', run],
                                    env=dict(env, SMOKE_MAX_CYCLES='0',
                                             SOFTWARE_TESTS='smoke host-mmio-test uart-pty-test'),
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            commands = [json.loads(line) for line in calls.read_text().splitlines()]
            self.assertEqual([command[3] for command in commands],
                             ['smoke', 'host-mmio-test', 'uart-pty-test'])
            self.assertFalse(any(arg.startswith('HTIF_ARGS=') for command in commands for arg in command))
            for command in (build, run):
                for exit_code in (0, 7):
                    calls.write_text('')
                    result = subprocess.run(['bash', '-eo', 'pipefail', '-c', command],
                                            env=dict(env, OPT_FAST='-O0', MAKE_EXIT=str(exit_code)),
                                            text=True, capture_output=True)
                    self.assertEqual(result.returncode == 0, exit_code == 0, result.stderr)
                    commands = [json.loads(line) for line in calls.read_text().splitlines()]
                    if command == run:
                        self.assertEqual(len(commands), 1)
                        self.assertEqual(commands[0][3], 'smoke')
                        self.assertIn('HTIF_ARGS=+max-cycles=100000', commands[0])
                        self.assertIn(f'PREBUILT_SIMULATOR={root}/simple-rv5stage-rva23-rsim/VTestDriver', commands[0])
                        self.assertIn('tool transcript', (root / 'simple-rv5stage-rva23-rsim-smoke.log').read_text())

    def test_software_selection_is_identical_for_matching_shape_and_isa(self):
        entries = self.plan("sims/Makefile")["simulator_matrix"]["include"]
        for (shape, isa), tests in SOFTWARE_TESTS.items():
            configs = [entry for entry in entries if entry["backend"] == 'circt' and (entry["shape"], entry["isa"]) == (shape, isa)]
            self.assertEqual({entry["core"] for entry in configs}, {"rv5stage", "spike"})
            self.assertEqual({entry["software_tests"] for entry in configs}, {" ".join(tests)})

    def test_rv32_native_inventory_is_paired_without_rv64_only_ports(self):
        entries = self.plan("sims/Makefile")["program_matrix"]["include"]
        for core in ("rv5stage", "spike"):
            for isa in ("rv32int", "rv32max"):
                self.assertEqual([entry["suite"] for entry in entries
                                  if entry["soc"] == f"simple-{core}-{isa}"], ["isa"])

    def test_rv64_smoke_presets_are_paired_isa_smoke_only(self):
        plan = self.plan("socs/configs/isa-profiles.rhm")
        presets = ("rv64max", "rv64imacb", "rv64imafdcb")
        configs = {f"simple-{core}-{isa}" for isa in presets for core in ("rv5stage", "spike")}
        runs = [entry for entry in plan["simulator_matrix"]["include"] if entry["isa"] in presets]
        self.assertEqual({entry["soc"] for entry in runs}, configs)
        self.assertTrue(all(entry["software_tests"] == "isa-smoke" for entry in runs))
        for matrix, key in (("program_matrix", "soc"), ("arch_build_matrix", "configuration"),
                            ("arch_run_matrix", "configuration")):
            self.assertFalse(any(entry[key].endswith(tuple(f"-{isa}" for isa in presets)) for entry in plan[matrix]["include"]))
        self.assertFalse(any(entry["soc"].endswith(tuple(f"-{isa}" for isa in presets)) for entry in platform_configs()))
        workflow = (REPO / '.github/workflows/ci-harness.yml').read_text()
        self.assertIn("if: contains(fromJSON(inputs.config).software_tests, 'isa-smoke')", workflow)
        self.assertEqual(workflow.count("if: always() && contains(fromJSON(inputs.config).software_tests, 'isa-smoke')"), 2)

    def test_software_only_builds_only_existing_single_core_configs(self):
        for path in ("sw/build/build-coremark.py", "sims/arch-test/configure.py", "sims/sail/configuration.py"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertEqual([entry["soc"] for entry in plan["simulator_matrix"]["include"]],
                                 list(SINGLE_CORE_SOCS))
                self.assertTrue(all("software_tests" not in entry for entry in plan["simulator_matrix"]["include"]))
                self.assertFalse(plan["run_simulation"])

    def test_native_builds_share_compatible_targets_and_preserve_every_run(self):
        entries = self.plan('sims/Makefile')['program_matrix']
        targets = {}
        for entry in entries['include']:
            soc = entry['soc']
            isa = soc.rsplit('-', 1)[1]
            xlen = 32 if isa.startswith('rv32') else 64
            targets[soc] = dict(soc=soc, xlen=xlen, harts=[0], extensions=['i', isa],
                                march=f'rv{xlen}i', mabi='ilp32' if xlen == 32 else 'lp64',
                                clock_frequency_hz=100000000, ram=[dict(base=0x80000000, size=0x10000)])
        matrices = program_matrices(entries, targets)
        self.assertEqual(len(matrices['build']['include']), 8)
        self.assertEqual(len(matrices['run']['include']), 16)
        self.assertEqual({(entry['soc'], entry['suite']) for entry in matrices['run']['include']},
                         {(entry['soc'], entry['suite']) for entry in entries['include']})
        builds = {entry['build_id'] for entry in matrices['build']['include']}
        self.assertEqual({entry['build_id'] for entry in matrices['run']['include']}, builds)
        selected_cosim = {entry['soc']: entry['cosim'] for entry in entries['include']}
        self.assertTrue(all(entry['cosim'] == selected_cosim[entry['soc']] for entry in matrices['run']['include']))
        targets['simple-spike-rva23']['clock_frequency_hz'] *= 2
        changed = program_matrices(entries, targets)
        self.assertEqual(len(changed['build']['include']), 10)  # Both CoreMark variants embed the clock.
        targets['simple-spike-rva23']['ram'][0]['size'] *= 2
        self.assertEqual(len(program_matrices(entries, targets)['build']['include']), 14)

    def test_shared_program_workflow_compiles_only_in_build_jobs(self):
        workflow = (REPO / '.github/workflows/ci-software.yml').read_text()
        build = workflow.split('  program-build:\n', 1)[1].split('  programs:\n', 1)[0]
        run = workflow.split('  programs:\n', 1)[1].split('  arch-sail:\n', 1)[0]
        self.assertIn('setup-riscv-toolchain', build)
        self.assertIn('PREBUILT_PROGRAM_TARGET=', build)
        self.assertNotIn('setup-riscv-toolchain', run)
        self.assertNotIn('Cache program builds', run)
        self.assertIn('sw/build/bind.py', run)
        self.assertIn('"$SUITE-run"', run)
        self.assertIn("always() && !cancelled() && needs.program-plan.result == 'success'", run)

    def test_shared_program_build_inputs_select_every_suite(self):
        for path in ("sw/build/program_target.py", "sw/build/bind.py", "sw/tests/test_program_build.py"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertEqual(suites(plan), list(NATIVE_SUITES))
                self.assertTrue(plan["run_program_arch"])

    def test_platforms_group_compatible_builds_without_removing_config_runs(self):
        entries = platform_configs()
        self.assertEqual({(entry['soc'], entry['suite']) for entry in entries}, {
            ('simple-rv5stage-rva23', 'opensbi'), ('simple-spike-rva23', 'opensbi'),
            ('tiled-rv5stage-rva23', 'litmus'), ('tiled-spike-rva23', 'litmus')})
        targets = {entry['soc']: dict(soc=entry['soc'], xlen=64,
                    harts=list(range(8)) if entry['suite'] == 'litmus' else [0],
                    extensions=['i', 'm', 'a', 'zicsr', 'zifencei', 'zicntr'],
                    march='rv64ima_zicsr_zifencei_zicntr', mabi='lp64',
                    clock_frequency_hz=100000000, boot=dict(payload_address=0x80000000),
                    ram=[dict(base=0x80000000, size=0x800000)]) for entry in entries}
        fdts = {entry['soc']: b'identical generated platform DTB' for entry in entries if entry['suite'] == 'opensbi'}
        plan = program_matrices(dict(include=entries), targets, fdts)
        self.assertEqual(len(plan['build']['include']), 2)
        self.assertEqual(len(plan['run']['include']), 4)
        for entry, run in zip(entries, plan['run']['include']):
            self.assertEqual({key: run[key] for key in entry}, entry)
        fdts['simple-spike-rva23'] = b'different devices'
        self.assertEqual(len(program_matrices(dict(include=entries), targets, fdts)['build']['include']), 3)
        targets['tiled-spike-rva23']['harts'] = [0, 1, 2, 3]
        self.assertEqual(len(program_matrices(dict(include=entries), targets, fdts)['build']['include']), 4)

    def test_platform_execution_consumes_only_prebuilt_artifacts(self):
        workflow = (REPO / '.github/workflows/ci-platform.yml').read_text()
        build = workflow.split('  platform-build:\n', 1)[1].split('  platform:\n', 1)[0]
        run = workflow.split('  platform:\n', 1)[1]
        self.assertIn('setup-riscv-toolchain', build)
        self.assertIn('Build pinned litmus7', build)
        self.assertIn('PREBUILT_PROGRAM_TARGET=', build)
        self.assertIn('PREBUILT_OPENSBI_FDT=', build)
        self.assertIn('sw/build/bind.py', run)
        self.assertIn('"$RUN_TARGET"', run)
        self.assertIn('timeout-minutes: ${{ matrix.timeout }}', run)
        self.assertIn("always() && !cancelled() && needs.platform-plan.result == 'success'", run)
        for compile_step in ('setup-riscv-toolchain', 'setup-racket', 'opam', 'litmus7', 'Cache platform builds'):
            self.assertNotIn(compile_step, run)

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
            "rhodium/compile/program.rhm": ("host-backend", "circt-language"),
            "rhodium/lowering/program.rhm": ("host-foundation", "host-backend", "circt-language"),
            "rhodium/event/instrument.rhm": ("host-foundation", "host-backend", "circt-language"),
            "flow/queue.rhdl": ("host-foundation", "host-backend", "host-protocols", "host-cores", "host-socs", "host-hygiene", "circt-std", "circt-protocols", "circt-core-cache", "host-examples"),
            "rhodium/backend/tests/circt/verilog/adder_tb.sv": ("host-backend", "circt-language", "circt-rfpl"),
            "devicetree/main.rhm": ("host-models", "host-hygiene"),
            "noc/rtl/router.rhdl": ("host-models", "host-socs", "circt-protocols", "host-examples"),
            "hardfloat/rtl/recode.rhdl": ("host-models", "circt-core-cache", "circt-hardfloat"),
            "chi/subordinate/dpi-memory.rhdl": ("host-protocols", "host-socs", "circt-protocols", "host-examples", "circt-verilog-differential"),
            "cores/rv5stage/core.rhdl": ("host-cores", "host-socs", "circt-core-execution-frontend", "circt-core-execution-control", "circt-core-execution-datapath", "host-examples"),
            "chi/subordinate/dpi/chi_dpi_memory_dpi.cc": ("circt-verilog-differential",),
            "socs/mini-rv5stage-soc.rhdl": ("host-socs", "circt-core-memory", "host-hygiene"),
            "examples/rfpl/circuit-pair.rhdl": ("host-examples", "circt-rfpl", "host-hygiene"),
            "tools/write-riscv-udb-config.rhm": ("host-models", "host-cores", "host-socs", "host-hygiene"),
            "sims/arch-test/platform.rhm": ("host-socs", "host-hygiene"),
            "sims/arch-test/write-platform.rhm": ("host-socs", "host-hygiene"),
            "sims/tests/config-test.rhm": ("host-socs", "host-hygiene"),
            "sims/cosim/pass.rhm": ("host-socs", "circt-core-components", "circt-core-execution-datapath", "host-hygiene"),
        }
        for path, expected in cases.items():
            with self.subTest(path=path):
                self.assert_checks(path, *expected)

    def test_flow_and_standard_library_share_the_base_plan(self):
        flow = self.plan("flow/queue.rhdl")
        standard = self.plan("rhodium/std/ready-valid.rhdl")
        # Event integrations now consume Flow as well as std through both targets.
        self.assertTrue({"verilog-direct", "circt-verilog-differential"} <= check_keys(flow))
        self.assertEqual(flow, standard)

    def test_simulation_only_paths_do_not_expand_host_checks(self):
        for path in ("sram/map-memories.py", "vlsi/sim/Makefile", "vlsi/designs/mini-rv5stage-soc/sky130/sram-map.yaml"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertTrue(plan["run_simulation"])
                self.assertFalse(plan["run_checks"])

    def test_cosim_receiver_selects_real_hook_transport(self):
        for path in ("sims/cosim/events/dpi.cc", "sims/cosim/events/collector.cc",
                     "sims/cosim/tests/events/hooks-fixture.rhdl", "sims/cosim/events/hooks.rhdl"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertIn("circt-core-components", check_keys(plan))
                self.assertTrue(plan["run_simulation"])

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

    def test_core_execution_shards_are_nonempty_disjoint_and_exhaustive(self):
        runner = REPO / "tools/testing/circt/run.sh"

        def fixtures(group):
            output = subprocess.run(["bash", runner, "--group", group, "--list-fixtures"], cwd=REPO, check=True, text=True, capture_output=True).stdout
            return set(output.splitlines())

        leaves = [fixtures(group) for group in ("cores-execution-frontend", "cores-execution-control", "cores-execution-datapath")]
        combined = fixtures("cores-execution")
        self.assertTrue(all(leaves))
        self.assertEqual(sum(map(len, leaves)), len(set.union(*leaves)))
        self.assertEqual(set.union(*leaves), combined)
        self.assertEqual(len(combined), 48)
        self.assertTrue({"rv2wide-core", "rv5stage-cosim", "rv5stage-cosim32", "rv5stage-cosim-vector"} <= leaves[2])

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
        for workflow in ("ci-checks.yml", "ci-simulator.yml", "ci-platform.yml", "ci-software.yml"):
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

    def test_project_bytecode_cache_precedes_exact_artifact(self):
        workflow = (REPO / ".github/workflows/ci.yml").read_text()
        compile_job = workflow.split("  compile-racket:\n", 1)[1].split("\n  checks:\n", 1)[0]
        for step in ("Restore completed bytecode", "Refresh restored project bytecode",
                     "Compile positive CI entrypoints", "Save completed bytecode",
                     "Publish exact compiled root"):
            self.assertIn(f"- name: {step}", compile_job)
        self.assertLess(compile_job.index("Restore completed bytecode"),
                        compile_job.index("Refresh restored project bytecode"))
        self.assertLess(compile_job.index("Refresh restored project bytecode"),
                        compile_job.index("Compile positive CI entrypoints"))
        self.assertLess(compile_job.index("Compile positive CI entrypoints"),
                        compile_job.index("Save completed bytecode"))
        self.assertLess(compile_job.index("Save completed bytecode"),
                        compile_job.index("Publish exact compiled root"))
        for step in ("Restore completed bytecode", "Save completed bytecode"):
            cache_step = compile_job.split(f"- name: {step}\n", 1)[1].split("\n      - name:", 1)[0]
            self.assertIn("${{ runner.temp }}/rhodium-compiled", cache_step)
            self.assertIn("racket-completed-v2", cache_step)
            self.assertIn("${{ env.RHODIUM_BYTECODE_WORKSPACE_KEY }}", cache_step)
        artifact_step = compile_job.split("- name: Publish exact compiled root\n", 1)[1]
        self.assertIn("include-hidden-files: true", artifact_step)
        self.assertIn("${{ runner.temp }}/rhodium-project-cache", compile_job)
        self.assertIn("${{ github.sha }}", compile_job)
        self.assertIn("tools/refresh-racket-project-cache.sh", compile_job)

    def test_config_workflows_follow_shape_policy(self):
        root = (REPO / ".github/workflows/ci.yml").read_text()
        build = (REPO / ".github/workflows/ci-simulator.yml").read_text()
        simulation = (REPO / ".github/workflows/ci-platform.yml").read_text()
        software = (REPO / ".github/workflows/ci-software.yml").read_text()
        self.assertIn(".simulator_matrix", root)
        harness = (REPO / '.github/workflows/ci-harness.yml').read_text()
        self.assertIn("config: ${{ toJSON(matrix) }}", root)
        self.assertIn("config: ${{ inputs.config }}", build)
        self.assertIn("name: ${{ fromJSON(inputs.config).simulator_id }}-${{ github.sha }}", build)
        self.assertIn("name: ${{ fromJSON(inputs.config).simulator_id }}-${{ github.sha }}", harness)
        self.assertIn("SOFTWARE_TESTS: ${{ fromJSON(inputs.config).software_tests }}", harness)
        self.assertIn('for target in $SOFTWARE_TESTS', harness)
        self.assertIn('make -C sims "$target" SOC="$SOC"', harness)
        self.assertIn("if: contains(fromJSON(inputs.config).software_tests, 'isa-smoke')", harness)
        self.assertIn('tiled-mt-benchmark-test', SOFTWARE_TESTS['tiled', 'rva23'])
        self.assertIn("platform-plan:", simulation)
        self.assertIn("platform_configs", simulation)
        self.assertIn("litmus-smoke-elfs", simulation)
        self.assertNotIn("litmus-full", simulation)
        self.assertIn("if: inputs.run-harness && fromJSON(inputs.config).backend == 'circt' && fromJSON(inputs.config).soc == 'simple-rv5stage-rva23'", build)
        self.assertIn('program-test-adapter-test cosim-hooks-test simulation-runtime-test', build)
        self.assertIn("matrix: ${{ fromJSON(inputs.arch-build-matrix) }}", software)
        self.assertIn("Restore pinned Spike runtime libraries", software)

    def test_config_harness_depends_only_on_its_own_simulator(self):
        root = (REPO / '.github/workflows/ci.yml').read_text()
        config = (REPO / '.github/workflows/ci-simulator.yml').read_text()
        simulator = root.split('  simulator:\n', 1)[1].split('  simulation:\n', 1)[0]
        self.assertIn('matrix: ${{ fromJSON(needs.plan.outputs.plan).simulator_matrix }}', simulator)
        self.assertIn('fail-fast: false', simulator)
        self.assertIn('run-harness: ${{ fromJSON(needs.plan.outputs.plan).run_simulation }}', simulator)
        harness = config.split('  harness:\n', 1)[1]
        self.assertIn('needs: build', harness)
        self.assertIn('if: inputs.run-harness', harness)
        self.assertIn('uses: ./.github/workflows/ci-harness.yml', harness)
        self.assertIn('config: ${{ inputs.config }}', harness)

    def test_harness_build_toolchains_are_scoped_to_supported_consumers(self):
        workflow = (REPO / '.github/workflows/ci-harness.yml').read_text()
        for name in ('Install Racket', 'Restore exact Rhodium bytecode', 'Install Verilator', 'Install CIRCT'):
            step = workflow.split(f'- name: {name}\n', 1)[1].split('\n      - name:', 1)[0]
            self.assertIn("if: fromJSON(inputs.config).soc == 'mini-rv5stage-rva23'", step)
        for name in ('Install mapped or Spike build dependencies', 'Install FESVR'):
            step = workflow.split(f'- name: {name}\n', 1)[1].split('\n      - name:', 1)[0]
            self.assertIn("if: fromJSON(inputs.config).soc == 'mini-rv5stage-rva23' || fromJSON(inputs.config).core == 'spike'", step)
        self.assertIn('Verify exact config and target descriptor', workflow)
        self.assertIn('PREBUILT_SIMULATOR="$simulator"', workflow)

    def test_simulator_reports_conversion_and_native_build_diagnostics(self):
        workflow = (REPO / '.github/workflows/ci-simulator.yml').read_text()
        self.assertIn("VERILATOR='verilator --stats'", workflow)
        self.assertIn('2>&1 | tee "$RUNNER_TEMP/$SIMULATOR_ID-build.log"', workflow)
        diagnostics = workflow.split('- name: Publish simulator build diagnostics\n', 1)[1].split('\n  harness:', 1)[0]
        self.assertIn('if: always()', diagnostics)
        self.assertIn('${{ steps.simulator.outputs.path }}/VTestDriver__stats*.txt', diagnostics)
        self.assertIn('name: simulator-build-${{ fromJSON(inputs.config).simulator_id }}-${{ github.sha }}', diagnostics)

    def test_cosim_uses_the_existing_config_artifacts_and_workloads(self):
        plan = self.plan('sims/Makefile')
        builds = plan['simulator_matrix']['include']
        expected = {'simple-rv5stage-rv64imacb', 'simple-rv5stage-rv64imafdcb',
                    'mini-rv5stage-rv32int', 'mini-rv5stage-rv32max',
                    'simple-rv5stage-rv32int', 'simple-rv5stage-rv32max',
                    'simple-rv5stage-rv64max', 'mini-rv5stage-rva23', 'simple-rv5stage-rva23'}
        self.assertEqual({row['soc'] for row in builds if row['cosim']}, expected)
        self.assertEqual(len(builds), len(SIMULATOR_CONFIGS) + len(BACKEND_SMOKE_VARIANTS))
        for soc in expected:
            build, = [row for row in builds if row['soc'] == soc and row['backend'] == 'circt']
            self.assertEqual(build['simulator_id'], soc)
            self.assertEqual(build['software_tests'].split(), list(SOFTWARE_TESTS[build['shape'], build['isa']]))
        for matrix, key in (('program_matrix', 'soc'), ('arch_run_matrix', 'configuration')):
            for entry in plan[matrix]['include']:
                self.assertEqual(entry['cosim'], entry[key] in expected)
        for entry in platform_configs():
            self.assertEqual(entry['cosim'], entry['soc'] in expected)
        direct, = [row for row in builds if row['backend'] == 'verilog']
        self.assertTrue(direct['cosim'])
        build = (REPO / '.github/workflows/ci-simulator.yml').read_text()
        workflow = (REPO / '.github/workflows/ci-harness.yml').read_text()
        self.assertIn('if: fromJSON(inputs.config).cosim', build)
        self.assertIn('run: make -C sims arch-test-sail-setup', build)
        self.assertIn("COSIM: ${{ fromJSON(inputs.config).cosim && '1' || '0' }}", build)
        self.assertIn('if [[ "$COSIM" == 1 ]]; then build="$build-cosim"; fi', build)
        self.assertIn('${{ steps.simulator.outputs.path }}/VTestDriver.json', build)
        self.assertIn('--variant "${{ fromJSON(inputs.config).cosim && \'cosim\' || \'normal\' }}"', workflow)
        run = workflow.split('- name: Run shape-and-ISA software selection\n', 1)[1].split('\n      - name:', 1)[0]
        self.assertIn("COSIM: ${{ fromJSON(inputs.config).cosim && '1' || '0' }}", run)
        self.assertIn('PREBUILT_SIMULATOR="$simulator"', run)
        self.assertIn('exit "$status"', run)
        reports = workflow.split('- name: Publish config smoke and multihart results\n', 1)[1].split('\n      - name:', 1)[0]
        self.assertIn('if: always()', reports)
        self.assertIn('/tmp/rhodium-program-tests/${{ fromJSON(inputs.config).soc }}/isa-smoke/results', reports)
        software = (REPO / '.github/workflows/ci-software.yml').read_text()
        consumers = [software.split('  programs:\n', 1)[1].split('  arch-sail:\n', 1)[0],
                     software.split('  arch:\n', 1)[1],
                     (REPO / '.github/workflows/ci-platform.yml').read_text().split('  platform:\n', 1)[1]]
        for consumer in consumers:
            self.assertIn('if: matrix.cosim', consumer)
            self.assertIn('libgmp10', consumer)
            self.assertIn('--variant "${{ matrix.cosim && \'cosim\' || \'normal\' }}"', consumer)
            self.assertIn("COSIM: ${{ matrix.cosim && '1' || '0' }}", consumer)

    def test_arch_execution_matrix_partitions_slow_configurations_without_extra_builds(self):
        workflow = (REPO / ".github/workflows/ci-software.yml").read_text()
        arch_job = workflow.split("  arch:\n", 1)[1]
        plan = self.plan("sims/Makefile")
        self.assertEqual({entry["configuration"] for entry in plan["arch_build_matrix"]["include"]}, set(SINGLE_CORE_SOCS))
        entries = plan["arch_run_matrix"]["include"]
        actual = {(entry["configuration"], entry["shard"]) for entry in entries}
        self.assertEqual(len(entries), len(actual))
        self.assertIn("matrix: ${{ fromJSON(inputs.arch-run-matrix) }}", arch_job)
        counts = {
            "simple-rv5stage-rva23": 16,
            "simple-rv5stage-rv32int": 8,
            "simple-rv5stage-rv32max": 8,
            "simple-spike-rva23": 8,
            "simple-spike-rv32int": 4,
            "simple-spike-rv32max": 4,
        }
        self.assertEqual(set(counts), set(SINGLE_CORE_SOCS))
        expected = {(configuration, shard) for configuration, count in counts.items()
                    for shard in range(count)}
        self.assertEqual(actual, expected)
        for entry in entries:
            self.assertEqual(entry["shard_count"], counts[entry["configuration"]])
        self.assertIn("ACT_SHARDS=${{ matrix.shard_count }}", arch_job)

    def test_arch_jobs_restore_and_check_every_spike_runtime(self):
        workflow = (REPO / ".github/workflows/ci-software.yml").read_text()
        arch_job = workflow.split("  arch:\n", 1)[1]
        for step_name in ("Install Spike runtime dependencies", "Restore pinned Spike runtime libraries"):
            with self.subTest(step=step_name):
                step = arch_job.split(f"      - name: {step_name}\n", 1)[1]
                self.assertTrue(step.startswith("        if: matrix.core == 'spike'\n"))
        self.assertIn('library_bindings="$(ldd "$RUNNER_TEMP/${{ matrix.configuration }}/VTestDriver")"', arch_job)
        self.assertIn('if [[ "$library_bindings" == *"not found"* ]]; then', arch_job)

    def test_arch_payload_cache_reuses_only_complete_exact_input_bundles(self):
        workflow = (REPO / ".github/workflows/ci-software.yml").read_text()
        build = workflow.split("  arch-build:\n", 1)[1].split("  arch:\n", 1)[0]
        run = workflow.split("  arch:\n", 1)[1]
        key = build.split("          ACT_INPUT_KEY: ", 1)[1].splitlines()[0]
        for input_name in ("sources.outputs.revision", "sources.outputs.sail_revision",
                           "act-config.outputs.digest", "act-config.outputs.tools",
                           "sims/arch-test/**", "sw/riscv-arch-test-patches/**",
                           "riscv/sail-riscv-patches/**", "fast1-all"):
            self.assertIn(input_name, key)
        self.assertNotIn("github.sha", key)
        self.assertNotIn("restore-keys:", build)
        for step_name in ("Cache ACT reference outputs", "Generate and package every selected ELF",
                          "Save complete successful ACT payload"):
            step = build.split(f"      - name: {step_name}\n", 1)[1].split("      - name:", 1)[0]
            self.assertIn("if: steps.act-payload.outputs.cache-hit != 'true'", step)
            self.assertNotIn("always()", step)
        cached = build.split("      - name: Verify cached ACT payload\n", 1)[1].split("      - name:", 1)[0]
        self.assertIn("if: steps.act-payload.outputs.cache-hit == 'true'", cached)
        self.assertIn("payload.py verify", cached)
        self.assertIn("--identity '${{ steps.act-inputs.outputs.key }}'", cached)
        generation = build.split("      - name: Generate and package every selected ELF\n", 1)[1].split("      - name:", 1)[0]
        self.assertIn("set -o pipefail", generation)
        self.assertLess(generation.index("make -C sims arch-test-elfs"), generation.index("payload.py package"))
        self.assertLess(build.index("payload.py package"), build.index("actions/cache/save@"))
        self.assertIn("inventory.json", build)
        publish = build.split("      - name: Publish exact-commit ACT payloads\n", 1)[1].split("      - name:", 1)[0]
        self.assertNotIn("if:", publish)
        self.assertIn("${{ github.sha }}", publish)
        self.assertIn("payload.py verify", run)
        self.assertIn("make -C sims arch-test-run", run)
        self.assertNotIn("cache-hit", run)


if __name__ == "__main__":
    unittest.main()
