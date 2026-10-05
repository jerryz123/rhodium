# Declares CI lanes and their runner requirements in one reviewable table.
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
    name: str
    target: str
    timeout: int = 30
    dtc: bool = False
    circt: bool = False
    verilator: bool = False

    def matrix_entry(self):
        return {
            "key": self.key,
            "name": self.name,
            "target": self.target,
            "timeout": self.timeout,
            "dtc": self.dtc,
            "circt": self.circt,
            "verilator": self.verilator,
        }


CHECKS = (
    Check("host-foundation", "Host / foundation", "ci-host-foundation-test"),
    Check("host-backend", "Host / backend", "ci-host-backend-test"),
    Check("host-models", "Host / models", "ci-host-models-test", dtc=True),
    Check("host-protocols", "Host / protocols", "ci-host-protocols-test"),
    Check("host-cores", "Host / cores", "ci-host-cores-test"),
    Check("host-socs", "Host / SoCs", "ci-host-socs-test", dtc=True),
    Check("host-hygiene", "Host / hygiene", "ci-host-hygiene-test"),
    Check("host-examples", "Host / auxiliary examples", "ci-host-examples-test", timeout=15),
    Check("verilog-direct", "SystemVerilog / direct smoke", "ci-verilog-direct-test", verilator=True),
    Check("circt-verilog-differential", "SystemVerilog / differential", "ci-backend-differential-test", circt=True, verilator=True),
    Check("circt-language", "CIRCT / language", "ci-circt-language-test", circt=True, verilator=True),
    Check("circt-std", "CIRCT / standard library", "ci-circt-std-test", circt=True, verilator=True),
    Check("circt-protocols", "CIRCT / protocols", "ci-circt-protocols-test", circt=True, verilator=True),
    Check("circt-core-components", "CIRCT / core components", "ci-circt-core-components-test", circt=True, verilator=True),
    Check("circt-core-execution-frontend", "CIRCT / core frontend", "ci-circt-core-execution-frontend-test", circt=True, verilator=True),
    Check("circt-core-execution-control", "CIRCT / core control", "ci-circt-core-execution-control-test", circt=True, verilator=True),
    Check("circt-core-execution-datapath", "CIRCT / core datapath", "ci-circt-core-execution-datapath-test", circt=True, verilator=True),
    Check("circt-core-vector-functional-1", "CIRCT / core vector functional 1", "ci-circt-core-vector-functional-1-test", circt=True, verilator=True),
    Check("circt-core-vector-functional-2", "CIRCT / core vector functional 2", "ci-circt-core-vector-functional-2-test", circt=True, verilator=True),
    Check("circt-core-vector-configurations", "CIRCT / core vector configurations", "ci-circt-core-vector-configurations-test", circt=True, verilator=True),
    Check("circt-core-memory", "CIRCT / core memory", "ci-circt-core-memory-test", circt=True, verilator=True),
    Check("circt-core-cache", "CIRCT / core caches", "ci-circt-core-cache-test", circt=True, verilator=True),
    Check("circt-hardfloat", "CIRCT / HardFloat", "hardfloat-circt-test", circt=True, verilator=True),
    Check("circt-rfpl", "CIRCT / RFPL", "rfpl-circt-test", circt=True, verilator=True),
)

CHECK_BY_KEY = {check.key: check for check in CHECKS}

HOST_CHECKS = frozenset(check.key for check in CHECKS if check.key.startswith("host-"))
CIRCT_CHECKS = frozenset(check.key for check in CHECKS if check.key.startswith("circt-"))
CIRCT_CORE_CHECKS = frozenset(
    check.key for check in CHECKS if check.key.startswith("circt-core-") or check.key == "circt-hardfloat"
)
NATIVE_SUITES = ("isa", "benchmark", "coremark", "embench", "bringup")
PLATFORM_TESTS = ("smoke", "host-mmio-test", "boot-test", "uart-pty-test")
# Software policy has exactly two axes. Core choice only selects the DUT.
SOFTWARE_TESTS = {
    ("mini", "rv32int"): PLATFORM_TESTS + ("isa-smoke",),
    ("mini", "rv32max"): PLATFORM_TESTS + ("isa-smoke",),
    ("mini", "rva23"): PLATFORM_TESTS + ("isa-smoke",),
    ("simple", "rva23"): PLATFORM_TESTS + ("zihintntl-test", "lrsc-test", "zicboz-test"),
    ("simple", "rv32int"): PLATFORM_TESTS,
    ("simple", "rv32max"): PLATFORM_TESTS,
    ("simple", "rv64max"): ("isa-smoke",),
    ("simple", "rv64imacb"): ("isa-smoke",),
    ("simple", "rv64imafdcb"): ("isa-smoke",),
    ("tiled", "rva23"): PLATFORM_TESTS + ("isa-smoke", "tiled-mt-benchmark-test"),
}
NATIVE_SOFTWARE = {("simple", "rva23"): NATIVE_SUITES,
                   ("simple", "rv32int"): ("isa",), ("simple", "rv32max"): ("isa",)}
SELECTIONS = selections()
TEST_CONFIGS = tuple(line for line in (ROOT / "sims/test-configs.txt").read_text().splitlines()
                      if line and not line.startswith("#"))
if len(TEST_CONFIGS) != len(set(TEST_CONFIGS)):
    raise ValueError("duplicate CI config")
SIMULATOR_CONFIGS = tuple((key, *SELECTIONS[key][:2]) for key in TEST_CONFIGS)
SINGLE_CORE_SOCS = tuple(soc for soc, shape, _core in SIMULATOR_CONFIGS
                         if (shape, SELECTIONS[soc][2]) in NATIVE_SOFTWARE)


def platform_configs():
    """Select OpenSBI and litmus tests by shape and ISA, never core identity."""
    suites = {'simple': ('opensbi', 'opensbi-smoke-run', 45),
              'tiled': ('litmus', 'litmus-smoke-run', 90)}
    return [dict(soc=soc, suite=suites[shape][0], run_target=suites[shape][1], timeout=suites[shape][2])
            for soc, shape, _core in SIMULATOR_CONFIGS
            if shape in suites and SELECTIONS[soc][2] == 'rva23']


def native_configs(suite):
    return tuple(soc for soc, shape, _core in SIMULATOR_CONFIGS
                 if suite in NATIVE_SOFTWARE.get((shape, SELECTIONS[soc][2]), ()))


# Backend variants reuse the architectural config and software target descriptor.
# Only simulator build/run matrices include this additional backend test.
BACKEND_SMOKE_CONFIG = ('simple-rv5stage-rva23', 'simple', 'rv5stage')
BACKEND_SMOKE_VARIANTS = {
    'verilog': dict(software_tests=('smoke', 'host-mmio-test', 'uart-pty-test')),
    'rsim': dict(software_tests=('smoke',), opt_fast='-O0',
                 smoke_max_cycles=100000, harness_timeout_minutes=5),
}


def simulator_entry(soc, shape, core, backend='circt'):
    return dict(soc=soc, shape=shape, core=core, backend=backend,
                simulator_id=soc + ('' if backend == 'circt' else '-' + backend),
                opt_fast=BACKEND_SMOKE_VARIANTS.get(backend, {}).get('opt_fast', ''))


def simulation_entry(soc, shape, core, backend='circt'):
    isa = SELECTIONS[soc][2]
    policy = BACKEND_SMOKE_VARIANTS.get(backend, {})
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
    'simple-spike-rva23': 8,
    'simple-spike-rv32int': 4,
    'simple-spike-rv32max': 4,
}


def arch_shards():
    return tuple(dict(**config, shard=shard, shard_count=count)
                 for config in arch_configs()
                 for count in (ARCH_SHARD_COUNTS[config['configuration']],)
                 for shard in range(count))
