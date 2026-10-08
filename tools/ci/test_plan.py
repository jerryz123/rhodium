# Tests CI selection, build sharing, shard coverage, and workflow failure propagation.
# SPDX-License-Identifier: Apache-2.0

import json
import hashlib
import os
import re
import tempfile
import textwrap
import shlex
import subprocess
import struct
import shutil
import tarfile
import unittest
from pathlib import Path
from unittest.mock import patch

from .gate import failures
from .plan import Selection, plan_for_paths
from .programs import program_matrices, harness_entries
from sw.build.program_target import elf_build_spec, target_fingerprint
from .policy import CHECKS, NATIVE_SUITES, SIMULATOR_CONFIGS, SINGLE_CORE_SOCS, SOFTWARE_TESTS, COSIM_CONFIGS, native_configs, simulation_entry, BACKEND_SMOKE_CONFIG, BACKEND_SMOKE_VARIANTS, platform_configs


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

    def test_rsim_component_selection_and_requirements(self):
        for path, expected in (
            ("flow/queue.rhdl", "rsim-std"),
            ("cores/tests/rsim/rv64i-alu.cpp", "rsim-core-components"),
            ("hardfloat/tests/rsim/numeric.cpp", "rsim-hardfloat"),
            ("chi/tests/rsim/chi-read-once.cpp", "rsim-protocols"),
            ("chi/tests/rsim/request.hpp", "rsim-core-components"),
            ("sims/tests/rsim/fesvr-mmio.cpp", "rsim-protocols"),
            ("sims/fesvr/direct-memory-htif.rhdl", "rsim-protocols"),
        ):
            self.assert_checks(path, expected)
        for path in ("tools/testing/rsim/run.py", "rhodium/backend/rsim.rhm"):
            plan = self.plan(path)
            entries = [entry for entry in plan["checks_matrix"]["include"]
                       if entry["key"].startswith("rsim-")]
            self.assertEqual(len(entries), 4)
            self.assertTrue(all(not entry["circt"] and not entry["verilator"] for entry in entries))

    def test_standard_library_goldens_need_no_simulator(self):
        self.assertFalse(next(check for check in CHECKS if check.key == "circt-std").verilator)

    def test_shared_core_ownership_audits_select_hygiene(self):
        for path in ("cores/check-boundaries.sh", "cores/tests/check-boundaries.sh"):
            with self.subTest(path=path):
                self.assert_checks(path, "host-hygiene")

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
        for path in ("sw/build/build.py", "sw/build/isa.mk", "sw/riscv-isa-tests"):
            with self.subTest(path=path):
                self.assertIn(simulation_entry("mini-rv5stage-rv32max", "mini", "rv5stage"),
                              self.plan(path)["simulator_matrix"]["include"])

    def test_direct_simulator_is_an_isolated_smoke_variant(self):
        for path in ('rhodium/backend/verilog.rhm', 'rhodium/compile/rtl.rhm',
                     'cores/rv5stage/core.rhdl', 'sims/Makefile', '.github/workflows/ci.yml'):
            with self.subTest(path=path):
                plan = self.plan(path)
                runs = plan['simulator_matrix']['include']
                self.assertEqual(len({entry['simulator_id'] for entry in runs}), len(runs))
                direct = [entry for entry in runs if entry['backend'] == 'verilog']
                self.assertEqual(len(direct), 1)
                self.assertEqual(direct[0]['soc'], 'simple-rv5stage-rva23')
                self.assertEqual(direct[0]['simulator_id'], 'simple-rv5stage-rva23-verilog')
                self.assertEqual(direct[0]['software_tests'].split(), ['smoke', 'host-mmio-test', 'uart-pty-test'])
                self.assertEqual({row['configuration'] for row in plan['arch_build_matrix']['include']}, set(SINGLE_CORE_SOCS))

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
                self.assertTrue(all(row['opt_fast'] for row in entries))
                self.assertGreater(rsim[0]['smoke_max_cycles'], 0)
                self.assertGreater(rsim[0]['harness_timeout_minutes'], 0)
                self.assertEqual({row['configuration'] for row in plan['arch_build_matrix']['include']}, set(SINGLE_CORE_SOCS))
                self.assertTrue(all(row['soc'] in SINGLE_CORE_SOCS for row in plan['program_matrix']['include']))
        # Software-only builds still use the ordinary simulator inventory.
        self.assertTrue(all(row['backend'] == 'circt' for row in
                            self.plan('sw/build/build-coremark.py')['simulator_matrix']['include']))
        self.assertTrue({row['soc'] for row in platform_configs()} <= {c[0] for c in SIMULATOR_CONFIGS})

    def test_backend_workflow_commands_propagate_settings_and_failures(self):
        def script(workflow, marker):
            text = (REPO / '.github/workflows' / workflow).read_text()
            # Extract literal shell blocks, not step labels or their ordering.
            blocks = [textwrap.dedent(match[2]) for match in re.finditer(
                r'(?m)^( +)run: \|[+-]?\n((?:(?:\1 +)[^\n]*\n|\n)+)', text)]
            matches = [block for block in blocks if marker in block]
            self.assertEqual(len(matches), 1, workflow)
            return matches[0]

        build = script('ci-simulator.yml', 'program-target')
        run = script('ci-harness.yml', 'PREBUILT_SIMULATOR')
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
                if Path(sys.argv[0]).name in ('mkdir', 'cp'):
                    sys.exit(0)
                with open(os.environ['CALL_LOG'], 'a') as output:
                    output.write(json.dumps([Path(sys.argv[0]).name, *sys.argv[1:]]) + '\\n')
                print('tool transcript')
                sys.exit(int(os.environ.get('MAKE_EXIT', '0')) if Path(sys.argv[0]).name == 'make' else 0)
            """)
            for name in ('make', 'ldd', 'mkdir', 'cp'):
                tool = root / name
                tool.write_text(stub)
                tool.chmod(0o755)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                       CALL_LOG=str(calls), RUNNER_TEMP=str(root), SOC='simple-rv5stage-rva23',
                       RTL_BACKEND='rsim', SIMULATOR_ID='simple-rv5stage-rva23-rsim',
                       SOFTWARE_TESTS='smoke', SMOKE_MAX_CYCLES='100000',
                       COSIM='0', GITHUB_OUTPUT=str(root / 'github-output'))
            (root / 'targets').mkdir()
            (root / 'targets/simple-rv5stage-rva23.json').write_text('{}')
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

    def test_shared_harness_binds_before_execution_and_preserves_later_attempts(self):
        workflow = (REPO / ".github/workflows/ci-harness.yml").read_text()
        block = workflow.split("      - name: Run shape-and-ISA software selection\n", 1)[1]
        script = textwrap.dedent(block.split("        run: |\n", 1)[1].split("      - name:", 1)[0])
        source = dict(soc="mini-rv5stage-rva23", xlen=64, harts=[0], extensions=["i", "m"],
                      march="rv64im", mabi="lp64", clock_frequency_hz=100000000,
                      ram=[dict(base=0x80000000, size=0x10000)])
        target = source | dict(soc="mini-spike-rva23")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("harness-elfs", "targets", target["soc"], "bin", "producer"):
                (root / name).mkdir()
            calls = root / "calls"
            make = root / "bin/make"
            make.write_text('#!/usr/bin/env python3\nimport json, os, sys\n'
                            'with open(os.environ["CALLS"], "a") as f: f.write(json.dumps(sys.argv[1:])+"\\n")\n')
            make.chmod(0o755)
            base = source["ram"][0]["base"]
            header = b"\x7fELF\x02\x01\x01" + bytes(9)
            header += struct.pack("<HHIQQQIHHHHHH", 2, 243, 1, base, 64, 0, 0, 64, 56, 1, 0, 0, 0)
            data = header + struct.pack("<IIQQQQQQ", 1, 5, 120, base, base, 1, 128, 1) + b"\x00"
            spec = elf_build_spec(source, "isa", dict(isa_selection="smoke"))
            manifest = dict(suite="isa", target=source, target_fingerprint=target_fingerprint(source),
                            build_spec=spec, build_spec_fingerprint=target_fingerprint(spec),
                            tests=[dict(name="test", elf="test.elf", sha256=hashlib.sha256(data).hexdigest())])
            (root / "producer/manifest.json").write_text(json.dumps(manifest))
            (root / "producer/test.elf").write_bytes(data)
            archive_path = root / "harness-elfs/shared.tar.gz"
            (root / target["soc"] / "program-target.json").write_text(json.dumps(target))
            (root / "targets/harness-plan.json").write_text(json.dumps(dict(run=dict(include=[
                dict(soc=target["soc"], run_target="isa-smoke-run", build_id="shared")]))))
            env = dict(os.environ, PATH=str(root / "bin") + os.pathsep + os.environ["PATH"],
                       RUNNER_TEMP=str(root), SOC=target["soc"], SIMULATOR_ID=target["soc"],
                       SOFTWARE_TESTS="isa-smoke smoke", CALLS=str(calls))
            script = script.replace("/tmp/rhodium-program-tests", str(root / "runs"))
            for bad in (False, True):
                with self.subTest(corrupt=bad):
                    (root / "producer/test.elf").write_bytes(b"corrupt" if bad else data)
                    with tarfile.open(archive_path, "w:gz") as archive:
                        for name in ("manifest.json", "test.elf"):
                            archive.add(root / "producer" / name, arcname=name)
                    calls.write_text("")
                    result = subprocess.run(["bash", "-eo", "pipefail", "-c", script],
                                            cwd=REPO, env=env, text=True, capture_output=True)
                    self.assertEqual(result.returncode, int(bad), result.stderr)
                    commands = [json.loads(line) for line in calls.read_text().splitlines()]
                    self.assertEqual([command[2] for command in commands],
                                     ["smoke"] if bad else ["isa-smoke-run", "smoke"])
                    if not bad:
                        bound = root / "runs" / target["soc"] / "isa-smoke/run-manifest.json"
                        self.assertIn(f"PROGRAM_MANIFEST={bound}", commands[0])
                        self.assertEqual(json.loads(bound.read_text())["target"], target)

    def test_software_selection_is_identical_for_matching_shape_and_isa(self):
        entries = self.plan("sims/Makefile")["simulator_matrix"]["include"]
        for (shape, isa), tests in SOFTWARE_TESTS.items():
            configs = [entry for entry in entries if entry["backend"] == 'circt' and (entry["shape"], entry["isa"]) == (shape, isa)]
            self.assertEqual({entry["core"] for entry in configs},
                             {core for soc, candidate_shape, core in SIMULATOR_CONFIGS
                              if (candidate_shape, soc.rsplit('-', 1)[1]) == (shape, isa)})
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
        configs.update(f"{shape}-rv2wide-{isa}" for shape in ("mini", "simple") for isa in ("rv64imacb", "rv64imafdcb"))
        runs = [entry for entry in plan["simulator_matrix"]["include"] if entry["isa"] in presets]
        self.assertEqual({entry["soc"] for entry in runs}, configs)
        self.assertTrue(all(entry["software_tests"] == "isa-smoke" for entry in runs))
        self.assertTrue(all(entry["cosim"] for entry in runs if entry["core"] == "rv2wide"))
        for matrix, key in (("program_matrix", "soc"), ("arch_build_matrix", "configuration"),
                            ("arch_run_matrix", "configuration")):
            self.assertFalse(any(entry[key].endswith(tuple(f"-{isa}" for isa in presets)) for entry in plan[matrix]["include"]))
        self.assertFalse(any(entry["soc"].endswith(tuple(f"-{isa}" for isa in presets)) for entry in platform_configs()))

    def test_software_only_builds_only_existing_single_core_configs(self):
        for path in ("sw/build/build-coremark.py", "sims/arch-test/configure.py", "sims/sail/configuration.py"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertEqual([entry["soc"] for entry in plan["simulator_matrix"]["include"]],
                                 list(SINGLE_CORE_SOCS))
                self.assertTrue(all("software_tests" not in entry for entry in plan["simulator_matrix"]["include"]))
                self.assertFalse(plan["run_simulation"])

    def test_native_builds_share_compatible_targets_and_preserve_every_run(self):
        def build_ids(matrix):
            return {(entry['soc'], entry['suite']): entry['build_id'] for entry in matrix['run']['include']}

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
        self.assertEqual(len(matrices['run']['include']), len(entries['include']))
        self.assertEqual({(entry['soc'], entry['suite']) for entry in matrices['run']['include']},
                         {(entry['soc'], entry['suite']) for entry in entries['include']})
        builds = {entry['build_id'] for entry in matrices['build']['include']}
        self.assertEqual({entry['build_id'] for entry in matrices['run']['include']}, builds)
        selected_cosim = {entry['soc']: entry['cosim'] for entry in entries['include']}
        self.assertTrue(all(entry['cosim'] == selected_cosim[entry['soc']] for entry in matrices['run']['include']))
        original = build_ids(matrices)
        for (soc, suite), build_id in original.items():
            paired = (soc.replace('rv5stage', 'spike'), suite)
            if paired in original:
                self.assertEqual(build_id, original[paired])
        targets['simple-spike-rva23']['clock_frequency_hz'] *= 2
        changed = build_ids(program_matrices(entries, targets))
        for (soc, suite), build_id in original.items():
            with self.subTest(change='clock', soc=soc, suite=suite):
                self.assertEqual(changed[soc, suite] != build_id,
                                 soc == 'simple-spike-rva23' and suite in ('coremark', 'coremark_scalar'))
        targets['simple-spike-rva23']['ram'][0]['size'] *= 2
        resized = build_ids(program_matrices(entries, targets))
        for (soc, suite), build_id in changed.items():
            with self.subTest(change='ram', soc=soc, suite=suite):
                self.assertEqual(resized[soc, suite] != build_id, soc == 'simple-spike-rva23')

    def test_shared_program_build_inputs_select_every_suite(self):
        for path in ("sw/build/program_target.py", "sw/build/bind.py", "sw/tests/test_program_build.py"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertEqual(suites(plan), list(NATIVE_SUITES))
                self.assertTrue(plan["run_program_arch"])

    def test_harness_sharing_preserves_runs_and_distinguishes_build_inputs(self):
        configs = [simulation_entry(f'{shape}-{core}-rva23', shape, core)
                   for shape in ('mini', 'tiled') for core in ('rv5stage', 'spike')]
        entries = harness_entries({'include': configs})
        targets = {config['soc']: dict(
            soc=config['soc'], xlen=64, harts=list(range(8)) if config['shape'] == 'tiled' else [0],
            extensions=['i', 'm', 'a', 'd'], march='rv64imad', mabi='lp64d',
            clock_frequency_hz=100000000,
            ram=[dict(base=0x80000000, size=0x100000 if config['shape'] == 'tiled' else 0x10000)])
            for config in configs}
        matrices = program_matrices(entries, targets)
        self.assertEqual(len(matrices['run']['include']), len(entries['include']))
        self.assertEqual({(e['soc'], e['run_target']) for e in matrices['run']['include']},
                         {(e['soc'], e['run_target']) for e in entries['include']})
        self.assertEqual(len(matrices['build']['include']), 3)
        def ids(matrix):
            return {(e['soc'], e['suite']): e['build_id'] for e in matrix['run']['include']}
        original = ids(matrices)
        for (soc, suite), build_id in original.items():
            self.assertEqual(build_id, original[soc.replace('rv5stage', 'spike'), suite])
        full = [dict(e, options=e['options'] | {'isa_selection': 'full'})
                for e in entries['include'] if e['suite'] == 'isa']
        self.assertTrue(set(ids(program_matrices({'include': full}, targets)).values()).isdisjoint(
            e['build_id'] for e in matrices['run']['include'] if e['suite'] == 'isa'))
        targets['tiled-spike-rva23']['harts'] = list(range(4))
        changed = ids(program_matrices(entries, targets))
        self.assertNotEqual(original['tiled-spike-rva23', 'benchmark'], changed['tiled-spike-rva23', 'benchmark'])
        self.assertEqual(original['tiled-spike-rva23', 'isa'], changed['tiled-spike-rva23', 'isa'])

    def test_platforms_group_compatible_builds_without_removing_config_runs(self):
        def build_ids(matrix):
            return {entry['soc']: entry['build_id'] for entry in matrix['run']['include']}

        entries = platform_configs()
        targets = {entry['soc']: dict(soc=entry['soc'], xlen=64,
                    harts=list(range(8)) if entry['suite'] == 'litmus' else [0],
                    extensions=['i', 'm', 'a', 'zicsr', 'zifencei', 'zicntr'],
                    march='rv64ima_zicsr_zifencei_zicntr', mabi='lp64',
                    clock_frequency_hz=100000000, boot=dict(payload_address=0x80000000),
                    ram=[dict(base=0x80000000, size=0x800000)]) for entry in entries}
        fdts = {entry['soc']: b'identical generated platform DTB' for entry in entries if entry['suite'] == 'opensbi'}
        plan = program_matrices(dict(include=entries), targets, fdts)
        self.assertEqual(len(plan['run']['include']), len(entries))
        original = build_ids(plan)
        for suite in {entry['suite'] for entry in entries}:
            self.assertEqual(len({original[entry['soc']] for entry in entries if entry['suite'] == suite}), 1)
        for entry, run in zip(entries, plan['run']['include']):
            self.assertEqual({key: run[key] for key in entry}, entry)
        fdts['simple-spike-rva23'] = b'different devices'
        changed = build_ids(program_matrices(dict(include=entries), targets, fdts))
        for soc, build_id in original.items():
            self.assertEqual(changed[soc] != build_id, soc == 'simple-spike-rva23')
        targets['tiled-spike-rva23']['harts'] = [0, 1, 2, 3]
        resized = build_ids(program_matrices(dict(include=entries), targets, fdts))
        for soc, build_id in changed.items():
            self.assertEqual(resized[soc] != build_id, soc == 'tiled-spike-rva23')

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
            "hardfloat/rtl/recode.rhdl": ("host-models", "circt-core-cache", "rsim-hardfloat"),
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

    def test_shared_jobs_follow_selected_simulator_capabilities(self):
        for path in ("README.md", "examples/rtl/alu.rhdl", "sims/Makefile", "sw/build/build-coremark.py"):
            with self.subTest(path=path):
                plan = self.plan(path)
                builds = plan["simulator_matrix"]["include"]
                self.assertEqual(plan["run_native"], bool(builds))
                self.assertEqual(plan["run_cosim"], any(row["cosim"] for row in builds))
                self.assertEqual(plan["run_spike_tests"], any(row["core"] == "spike" for row in builds))

    def test_check_declarations_are_unique_and_self_consistent(self):
        self.assertEqual(len({check.key for check in CHECKS}), len(CHECKS))
        self.assertEqual(len({check.target for check in CHECKS}), len(CHECKS))
        for check in CHECKS:
            with self.subTest(check=check.key):
                self.assertTrue(check.name)
                self.assertGreater(check.timeout, 0)
                # Lowering-only lanes need CIRCT without an HDL simulator.
                self.assertIsInstance(check.circt, bool)
                self.assertIsInstance(check.verilator, bool)

    def test_vector_functional_shards_partition_the_aggregate(self):
        runner = REPO / "tools/testing/circt/run.sh"

        def fixtures(group):
            output = subprocess.run(["bash", runner, "--group", group, "--list-fixtures"], cwd=REPO, check=True, text=True, capture_output=True).stdout
            return set(output.splitlines())

        first = fixtures("cores-vector-functional-1")
        second = fixtures("cores-vector-functional-2")
        combined = fixtures("cores-vector-functional")
        self.assertTrue(first)
        self.assertTrue(second)
        self.assertTrue(first.isdisjoint(second))
        self.assertEqual(first | second, combined)

    def test_core_execution_shards_partition_the_aggregate(self):
        runner = REPO / "tools/testing/circt/run.sh"

        def fixtures(group):
            output = subprocess.run(["bash", runner, "--group", group, "--list-fixtures"], cwd=REPO, check=True, text=True, capture_output=True).stdout
            return set(output.splitlines())

        leaves = [fixtures(group) for group in ("cores-execution-frontend", "cores-execution-control", "cores-execution-datapath")]
        combined = fixtures("cores-execution")
        self.assertTrue(all(leaves))
        self.assertEqual(sum(map(len, leaves)), len(set.union(*leaves)))
        self.assertEqual(set.union(*leaves), combined)
        self.assertTrue({"rv2wide-core", "rv2wide-fetch-disabled", "rv2wide-assembly-prediction", "rv5stage-cosim", "rv5stage-cosim32", "rv5stage-cosim-vector"} <= leaves[2])

    def test_shared_predictor_fixtures_belong_to_components(self):
        runner = REPO / "tools/testing/rsim/run.py"
        output = subprocess.run(["python3", runner, "--group", "cores-components", "--list"], cwd=REPO, check=True, text=True, capture_output=True).stdout
        self.assertTrue({"bpred-btb", "bpred-btb-wide", "bpred-ras"} <= set(output.splitlines()))

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

    def test_gate_requires_success_for_selected_workflows(self):
        for path in ("README.md", "examples/rtl/alu.rhdl", "sims/Makefile", "sw/build/build-coremark.py", "sram/map-memories.py"):
            plan = self.plan(path)
            selected = dict(compile=plan["run_compile"], checks=plan["run_checks"],
                            sail=plan["run_sail"],
                            targets=plan["run_simulator"], native=plan["run_native"],
                            **{'harness-elfs': plan['run_simulation']},
                            simulator=plan["run_simulator"], simulation=plan["run_simulation"],
                            software=plan["run_program_native"] or plan["run_program_arch"])
            results = {job: "success" if required else "skipped" for job, required in selected.items()}
            self.assertEqual(failures(plan, results), [])
            for job, required in selected.items():
                for result in ("success", "failure", "cancelled", "skipped", None):
                    with self.subTest(path=path, job=job, required=required, result=result):
                        actual = results | {job: result}
                        expected = [] if result == "success" or (not required and result == "skipped") else [
                            f"{'selected' if required else 'unselected'} job {job} finished with {result}"]
                        self.assertEqual(failures(plan, actual), expected)
                missing = dict(results)
                del missing[job]
                self.assertEqual(failures(plan, missing), [
                    f"{'selected' if required else 'unselected'} job {job} finished with None"])

    def test_cosim_uses_the_existing_config_artifacts_and_workloads(self):
        plan = self.plan('sims/Makefile')
        builds = plan['simulator_matrix']['include']
        expected = COSIM_CONFIGS
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

    def test_shared_sail_is_selected_for_act_or_cosim_consumers(self):
        for path in ("README.md", "examples/rtl/alu.rhdl", "sims/Makefile",
                     "sw/build/build-coremark.py", "sims/arch-test/setup.sh"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertEqual(plan["run_sail"], plan["run_program_arch"] or any(
                    row["cosim"] for row in plan["simulator_matrix"]["include"]))
        self.assertFalse(self.plan("examples/rtl/alu.rhdl")["run_sail"])
        self.assertTrue(self.plan("sw/build/build-coremark.py")["run_sail"])
        self.assertTrue(self.plan("sims/arch-test/setup.sh")["run_sail"])
        with patch("tools.ci.policy.COSIM_CONFIGS", set()):
            self.assertFalse(Selection(native_suites={"coremark"}).result()["run_sail"])
            self.assertTrue(Selection(arch=True).result()["run_sail"])

    def test_sail_archive_handoff_preserves_libraries_and_completion_marker(self):
        def script(workflow, name):
            text = (REPO / ".github/workflows" / workflow).read_text()
            step = text.split(f"      - name: {name}\n", 1)[1].split("      - name:", 1)[0]
            return textwrap.dedent(step.split("        run: |\n", 1)[1])

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            producer = root / "producer"
            producer.mkdir()
            subprocess.run(["git", "init", "-q", str(producer)], check=True)
            revision = subprocess.check_output(["git", "rev-parse", "HEAD:riscv/sail-riscv"],
                                               cwd=REPO, text=True).strip()
            subprocess.run(["git", "update-index", "--add", "--cacheinfo",
                            f"160000,{revision},riscv/sail-riscv"], cwd=producer, check=True)
            (producer / "riscv").mkdir()
            shutil.copy(REPO / "riscv/patched_submodule.py", producer / "riscv")
            shutil.copytree(REPO / "riscv/sail-riscv-patches", producer / "riscv/sail-riscv-patches")
            identity = subprocess.check_output([
                "python3", "riscv/patched_submodule.py", "identity", "--repository", ".",
                "--submodule", "riscv/sail-riscv", "--series", "riscv/sail-riscv-patches/series"],
                cwd=producer, text=True).strip()
            prefix = f"sail-riscv-0.14.1-sail-0.20.3-{identity}"
            model = producer / ".tools" / prefix
            (model / "bin").mkdir(parents=True)
            (model / "lib").mkdir()
            (model / ".complete").write_text(identity + "\n")
            (model / "lib/libsail_riscv_model.a").write_bytes(b"model library")
            executable = model / "bin/sail_riscv_sim"
            executable.write_text("#!/bin/sh\necho 0.14.1\n")
            executable.chmod(0o755)
            env = {**os.environ, "RUNNER_TEMP": str(root / "artifacts")}
            subprocess.run(["bash", "-euo", "pipefail", "-c",
                            script("ci.yml", "Package patched Sail model")],
                           cwd=producer, env=env, check=True)
            runtime_relative = Path(f".rhodium-cache/sail-cosim/sail-0.20.3-{identity}")
            runtime = producer / runtime_relative
            runtime.mkdir(parents=True)
            (runtime / "libsail_checker.a").write_bytes(b"checker library")
            with tarfile.open(root / "artifacts/sail-riscv/runtime.tar.gz", "w:gz") as archive:
                archive.add(runtime, arcname=str(runtime_relative))
            for workflow in ("ci-simulator.yml", "ci-software.yml"):
                consumer = root / workflow
                consumer.mkdir()
                subprocess.run(["bash", "-euo", "pipefail", "-c",
                                script(workflow, "Install patched Sail model")],
                               cwd=consumer, env=env, check=True)
                installed = consumer / ".tools" / prefix
                self.assertEqual((installed / ".complete").read_text(), identity + "\n")
                self.assertEqual((installed / "lib/libsail_riscv_model.a").read_bytes(), b"model library")
                self.assertEqual(subprocess.check_output([str(installed / "bin/sail_riscv_sim"),
                                                         "--version"], text=True), "0.14.1\n")
                if workflow == "ci-simulator.yml":
                    self.assertEqual((consumer / runtime_relative / "libsail_checker.a").read_bytes(),
                                     b"checker library")

    def test_arch_execution_matrix_partitions_slow_configurations_without_extra_builds(self):
        plan = self.plan("sims/Makefile")
        self.assertEqual({entry["configuration"] for entry in plan["arch_build_matrix"]["include"]}, set(SINGLE_CORE_SOCS))
        entries = plan["arch_run_matrix"]["include"]
        actual = {(entry["configuration"], entry["shard"]) for entry in entries}
        self.assertEqual(len(entries), len(actual))
        self.assertEqual({entry["configuration"] for entry in entries}, set(SINGLE_CORE_SOCS))
        for configuration in SINGLE_CORE_SOCS:
            selected = [entry for entry in entries if entry["configuration"] == configuration]
            counts = {entry["shard_count"] for entry in selected}
            self.assertEqual(len(counts), 1)
            count, = counts
            self.assertGreater(count, 0)
            self.assertEqual({entry["shard"] for entry in selected}, set(range(count)))



if __name__ == "__main__":
    unittest.main()
