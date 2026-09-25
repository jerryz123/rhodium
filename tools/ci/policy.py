# Declares CI lanes and their runner requirements in one reviewable table.
# SPDX-License-Identifier: Apache-2.0

from dataclasses import dataclass


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
    Check("example-rtl", "Examples / Rhodium", "examples-rhodium", timeout=15),
    Check("example-clocking", "Examples / clocking analysis", "examples-clocking", timeout=15),
    Check("example-std", "Examples / standard library", "examples-std", timeout=15),
    Check("example-noc", "Examples / NoC", "examples-noc", timeout=15),
    Check("example-lop", "Examples / language-oriented programming", "examples-lop", timeout=15),
    Check("example-rfpl", "Examples / RFPL", "examples-rfpl", timeout=15),
    Check("example-riscv", "Examples / RISC-V", "examples-riscv", timeout=15),
    Check("example-chi", "Examples / CHI", "examples-chi", timeout=15),
    Check("example-cores", "Examples / processor cores", "examples-cores", timeout=15),
    Check("example-rv5stage", "Examples / RV5Stage", "examples-rv5stage", timeout=15),
    Check("circt-language", "CIRCT / language", "ci-circt-language-test", circt=True, verilator=True),
    Check("circt-std", "CIRCT / standard library", "ci-circt-std-test", circt=True, verilator=True),
    Check("circt-protocols", "CIRCT / protocols", "ci-circt-protocols-test", circt=True, verilator=True),
    Check("circt-core-components", "CIRCT / core components", "ci-circt-core-components-test", circt=True, verilator=True),
    Check("circt-core-execution", "CIRCT / core execution", "ci-circt-core-execution-test", circt=True, verilator=True),
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
EXAMPLE_CHECKS = frozenset(check.key for check in CHECKS if check.key.startswith("example-"))
CIRCT_CHECKS = frozenset(check.key for check in CHECKS if check.key.startswith("circt-"))
CIRCT_CORE_CHECKS = frozenset(
    check.key for check in CHECKS if check.key.startswith("circt-core-") or check.key == "circt-hardfloat"
)
NATIVE_SUITES = ("isa", "benchmark", "coremark", "embench", "bringup")
PLATFORM_TESTS = ("smoke", "host-mmio-test", "boot-test", "uart-pty-test")
# Software policy has exactly two axes. Core choice only selects the DUT.
SOFTWARE_TESTS = {
    ("mini", "rv32max"): PLATFORM_TESTS + ("isa-smoke",),
    ("mini", "rva23"): PLATFORM_TESTS + ("isa-smoke",),
    ("single", "rva23"): PLATFORM_TESTS + ("zihintntl-test", "lrsc-test", "zicboz-test"),
    ("tiled", "rva23"): PLATFORM_TESTS + ("isa-smoke", "tiled-mt-benchmark-test"),
}
NATIVE_SOFTWARE = {("single", "rva23"): NATIVE_SUITES}
SIMULATOR_PRODUCTS = (
    ("mini-rv5stage-rv32max", "mini", "rv5stage"),
    ("mini-spike-rv32max", "mini", "spike"),
    ("mini-rv5stage-rva23", "mini", "rv5stage"),
    ("mini-spike-rva23", "mini", "spike"),
    ("simple-rv5stage-rva23", "single", "rv5stage"),
    ("simple-spike-rva23", "single", "spike"),
    ("tiled-rv5stage-rva23", "tiled", "rv5stage"),
    ("tiled-spike-rva23", "tiled", "spike"),
)
SINGLE_CORE_SOCS = tuple(soc for soc, shape, _core in SIMULATOR_PRODUCTS
                         if (shape, soc.rsplit("-", 1)[1]) in NATIVE_SOFTWARE)


def simulation_entry(soc, shape, core):
    isa = soc.rsplit("-", 1)[1]
    return dict(soc=soc, shape=shape, core=core, isa=isa,
                software_tests=" ".join(SOFTWARE_TESTS[shape, isa]))
