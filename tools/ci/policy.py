# Declares CI coverage, compatible execution groups, and runner requirements.
# SPDX-License-Identifier: Apache-2.0

from dataclasses import dataclass
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "socs/configs"))
from selection import selections


@dataclass(frozen=True)
class Check:
    key: str
    target: str
    timeout: int = 30
    dtc: bool = False
    circt: bool = False
    verilator: bool = False


CHECKS = (
    Check("host-foundation", "ci-host-foundation-test"),
    Check("host-backend", "ci-host-backend-test"),
    Check("host-models", "ci-host-models-test", dtc=True),
    Check("host-protocols", "ci-host-protocols-test"),
    Check("host-cores", "ci-host-cores-test"),
    Check("host-socs", "ci-host-socs-test", dtc=True),
    Check("host-hygiene", "ci-host-hygiene-test"),
    Check("host-examples", "ci-host-examples-test", timeout=15),
    Check("verilog-direct", "ci-verilog-direct-test", verilator=True),
    Check("circt-verilog-differential", "ci-backend-differential-test", circt=True, verilator=True),
    Check("rsim-std", "ci-rsim-std-test"),
    Check("rsim-core-components", "ci-rsim-core-components-test"),
    Check("rsim-core-execution-frontend", "ci-rsim-core-execution-frontend-test"),
    Check("rsim-core-execution-control", "ci-rsim-core-execution-control-test"),
    Check("rsim-core-execution-datapath", "ci-rsim-core-execution-datapath-test"),
    Check("rsim-core-vector-functional-1", "ci-rsim-core-vector-functional-1-test"),
    Check("rsim-core-vector-functional-2", "ci-rsim-core-vector-functional-2-test"),
    Check("rsim-core-vector-configurations", "ci-rsim-core-vector-configurations-test"),
    Check("rsim-core-memory", "ci-rsim-core-memory-test"),
    Check("rsim-core-cache", "ci-rsim-core-cache-test"),
    Check("rsim-protocols", "ci-rsim-protocols-test"),
    Check("circt-language", "ci-circt-language-test", circt=True, verilator=True),
    Check("circt-std", "ci-circt-std-test", circt=True),
    Check("circt-protocols", "ci-circt-protocols-test", circt=True, verilator=True),
    Check("circt-core-components", "ci-circt-core-components-test", circt=True, verilator=True),
    Check("circt-core-execution-datapath", "ci-circt-core-execution-datapath-test", circt=True, verilator=True),
    Check("rsim-hardfloat", "hardfloat-rsim-test"),
    Check("circt-rfpl", "rfpl-circt-test", circt=True, verilator=True),
)

CHECK_BY_KEY = {check.key: check for check in CHECKS}

# Selection remains per check; only selected members share a runner and tools.
CHECK_GROUPS = (
    ("host-language", "Host / language", ("host-foundation", "host-backend", "host-hygiene", "host-examples")),
    ("host-systems", "Host / systems", ("host-models", "host-protocols", "host-cores", "host-socs")),
    ("rsim-library", "Rsim / libraries", ("rsim-std", "rsim-hardfloat")),
    ("rsim-memory", "Rsim / memory and protocols", ("rsim-protocols", "rsim-core-memory", "rsim-core-cache")),
    ("rsim-components", "Rsim / core components", ("rsim-core-components",)),
    ("rsim-execution", "Rsim / core execution", ("rsim-core-execution-frontend", "rsim-core-execution-control", "rsim-core-execution-datapath")),
    ("rsim-vector", "Rsim / vector execution", ("rsim-core-vector-functional-1", "rsim-core-vector-functional-2")),
    ("rsim-vector-configurations", "Rsim / vector configurations", ("rsim-core-vector-configurations",)),
    ("verilog", "SystemVerilog / backends", ("verilog-direct", "circt-verilog-differential")),
    ("circt", "CIRCT / emission and integration", ("circt-language", "circt-std", "circt-protocols", "circt-core-components", "circt-core-execution-datapath", "circt-rfpl")),
)
grouped_checks = [member for _key, _name, members in CHECK_GROUPS for member in members]
if len(grouped_checks) != len(CHECKS) or set(grouped_checks) != set(CHECK_BY_KEY):
    raise ValueError("CI check groups must partition the check inventory")


def check_matrix(selected):
    """Share setup without expanding the checks selected by changed paths."""
    if set(selected) - CHECK_BY_KEY.keys():
        raise ValueError("unknown CI check")
    rows = []
    for key, name, members in CHECK_GROUPS:
        checks = [CHECK_BY_KEY[member] for member in members if member in selected]
        if checks:
            rows.append(dict(key=key, name=name, targets=[check.target for check in checks],
                             timeout=max(check.timeout for check in checks),
                             **{tool: any(getattr(check, tool) for check in checks)
                                for tool in ("dtc", "circt", "verilator")}))
    return {"include": rows}


HOST_CHECKS = frozenset(check.key for check in CHECKS if check.key.startswith("host-"))
CIRCT_CHECKS = frozenset(check.key for check in CHECKS if check.key.startswith("circt-"))
CORE_CHECKS = frozenset(
    check.key for check in CHECKS if check.key.startswith("circt-core-") or check.key.startswith("rsim-core-") or check.key == "rsim-hardfloat"
)
RSIM_CORE_CHECKS = frozenset(key for key in CORE_CHECKS if key.startswith("rsim-"))
RSIM_COMPONENT_CHECKS = frozenset(check.key for check in CHECKS if check.key.startswith("rsim-"))
NATIVE_SUITES = ("isa", "benchmark", "coremark", "embench", "bringup", "skl", "highway")
PROGRAM_SHARD_COUNTS = {"bringup": 4}
SHORT_PROGRAM_SUITES = frozenset(("isa", "benchmark", "coremark", "coremark_scalar", "embench"))
PLATFORM_TESTS = ("smoke", "host-mmio-test", "boot-test", "uart-pty-test")
# Software policy has exactly two axes. Core choice only selects the DUT.
SOFTWARE_TESTS = {
    ("mini", "rv32int"): PLATFORM_TESTS + ("isa-smoke",),
    ("mini", "rv32max"): PLATFORM_TESTS + ("isa-smoke",),
    ("mini", "rva23"): PLATFORM_TESTS + ("isa-smoke",),
    ("mini", "rv64imacb"): ("isa-smoke",),
    ("mini", "rv64imafdcb"): ("isa-smoke",),
    ("simple", "rva23"): PLATFORM_TESTS + ("zihintntl-test", "lrsc-test", "zicboz-test"),
    ("simple", "rv32int"): PLATFORM_TESTS,
    ("simple", "rv32max"): PLATFORM_TESTS,
    ("simple", "rv64max"): ("isa-smoke",),
    ("simple", "rv64imacb"): ("isa-smoke",),
    ("simple", "rv64imafdcb"): ("isa-smoke",),
    ("tiled4core", "rva23"): PLATFORM_TESTS + ("isa-smoke",),
    ("tiled8core", "rva23"): PLATFORM_TESTS + ("isa-smoke", "tiled-mt-benchmark-test"),
}
NATIVE_SOFTWARE = {("simple", "rva23"): NATIVE_SUITES,
                   ("simple", "rv32int"): ("isa",), ("simple", "rv32max"): ("isa",)}
SELECTIONS = selections()
TEST_CONFIGS = tuple(line for line in (ROOT / "sims/test-configs.txt").read_text().splitlines()
                      if line and not line.startswith("#"))
if len(TEST_CONFIGS) != len(set(TEST_CONFIGS)):
    raise ValueError("duplicate CI config")
# Hardware implementations must exercise the same shape/ISA selections.
paired_selections = {
    core: {(SELECTIONS[soc][0], SELECTIONS[soc][2]) for soc in TEST_CONFIGS
           if SELECTIONS[soc][1] == core}
    for core in ('rv5stage', 'rv2wide')
}
if paired_selections['rv5stage'] != paired_selections['rv2wide']:
    raise ValueError("RV5Stage and RV2Wide CI shape/ISA selections must match")
SIMULATOR_CONFIGS = tuple((key, *SELECTIONS[key][:2]) for key in TEST_CONFIGS)
SINGLE_CORE_SOCS = tuple(soc for soc, shape, _core in SIMULATOR_CONFIGS
                         if (shape, SELECTIONS[soc][2]) in NATIVE_SOFTWARE)


def platform_configs():
    """Select OpenSBI and litmus tests by shape and ISA, never core identity."""
    suites = {'simple': ('opensbi', 'opensbi-smoke-run', 45),
              'tiled4core': ('litmus', 'litmus-smoke-run', 90)}
    return [dict(soc=soc, suite=suites[shape][0], run_target=suites[shape][1], timeout=suites[shape][2],
                 **simulator_options(soc), backend=DEFAULT_BACKEND)
            for soc, shape, _core in SIMULATOR_CONFIGS
            if shape in suites and SELECTIONS[soc][2] == 'rva23']


def linux_configs():
    """Select build-only Linux images by shape and ISA, independently of simulation."""
    return [dict(soc=soc, suite='linux') for soc, shape, _core in SIMULATOR_CONFIGS
            if shape == 'simple' and SELECTIONS[soc][2] == 'rva23']


def native_configs(suite):
    return tuple(soc for soc, shape, _core in SIMULATOR_CONFIGS
                 if suite in NATIVE_SOFTWARE.get((shape, SELECTIONS[soc][2]), ()))


# Backend variants reuse the architectural config and software target descriptor.
# Only simulator build/run matrices include this additional backend test.
BACKEND_SMOKE_CONFIGS = (
    ('simple-rv5stage-rva23', 'simple', 'rv5stage'),
    ('simple-rv2wide-rva23', 'simple', 'rv2wide'),
)
DEFAULT_BACKEND = 'rsim'
BACKEND_SMOKE_VARIANTS = {
    'verilog': dict(software_tests=('smoke', 'host-mmio-test', 'uart-pty-test')),
    'circt': dict(software_tests=('smoke', 'boot-test', 'host-mmio-test', 'uart-pty-test', 'isa-smoke'),
                  harness_timeout_minutes=60),
}

# These existing configs publish one cosim-enabled simulator, not another lane.
# Enrolled single-hart RTL cores use the checker without changing software selection.
COSIM_CONFIGS = frozenset(soc for soc, shape, core in SIMULATOR_CONFIGS
                           if core in ('rv5stage', 'rv2wide') and shape in ('mini', 'simple'))


def simulator_options(soc):
    """Keep instrumentation and artifact identity identical for every build and consumer."""
    shape, core, isa = SELECTIONS[soc]
    cosim = soc in COSIM_CONFIGS
    stats = core in ('rv5stage', 'rv2wide') and shape in ('simple', 'tiled') and isa == 'rva23'
    return dict(cosim=cosim, stats=stats,
                variant=('cosim' if cosim else 'normal') + ('-stats' if stats else ''))


def simulator_entry(soc, shape, core, backend=DEFAULT_BACKEND):
    return dict(soc=soc, shape=shape, core=core, backend=backend,
                **simulator_options(soc),
                simulator_id=soc + ('' if backend == DEFAULT_BACKEND else '-' + backend),
                opt_fast='-O2')


def simulation_entry(soc, shape, core, backend=DEFAULT_BACKEND, *, smoke_variant=False):
    isa = SELECTIONS[soc][2]
    policy = BACKEND_SMOKE_VARIANTS[backend] if smoke_variant else {}
    tests = policy.get('software_tests', SOFTWARE_TESTS[shape, isa])
    return dict(**simulator_entry(soc, shape, core, backend), isa=isa,
                software_tests=" ".join(tests),
                smoke_max_cycles=policy.get('smoke_max_cycles', 0),
                harness_timeout_minutes=policy.get('harness_timeout_minutes', 60))


def arch_configs():
    return tuple(dict(configuration=key, core=core) for key, shape, core in SIMULATOR_CONFIGS
                 if shape == 'simple' and SELECTIONS[key][2] in ('rv32int', 'rv32max', 'rva23'))


# Resource partitioning may depend on implementation speed; test coverage does not.
ARCH_SHARD_COUNTS = {
    'simple-rv5stage-rva23': 16,
    'simple-rv5stage-rv32int': 8,
    'simple-rv5stage-rv32max': 8,
    'simple-rv2wide-rv32int': 8,
    'simple-rv2wide-rv32max': 8,
    'simple-rv2wide-rva23': 16,
    'simple-spike-rva23': 8,
    'simple-spike-rv32int': 4,
    'simple-spike-rv32max': 4,
}


def arch_shards():
    return tuple(dict(**config, shard=shard, shard_count=count,
                      **simulator_options(config['configuration']), backend=DEFAULT_BACKEND)
                 for config in arch_configs()
                 for count in (ARCH_SHARD_COUNTS[config['configuration']],)
                 for shard in range(count))
