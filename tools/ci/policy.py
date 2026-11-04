# Declares hardware/software coverage and the enrolled Moon workload configurations.
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

NATIVE_SUITES = ('isa', 'benchmark', 'coremark', 'embench', 'bringup', 'skl', 'highway')

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "socs/configs"))
from selection import selections


PROGRAM_SHARD_COUNTS = {"bringup": 4}
COREMARK_SUITES = frozenset(("coremark", "coremark_scalar"))


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
    for core in ('rv5stage', 'rv2wide', 'rv3wide')
}
if any(selected != paired_selections['rv5stage'] for selected in paired_selections.values()):
    raise ValueError("RV5Stage, RV2Wide, and RV3Wide CI shape/ISA selections must match")
SIMULATOR_CONFIGS = tuple((key, *SELECTIONS[key][:2]) for key in TEST_CONFIGS)
def software_configs(local):
    yield from SIMULATOR_CONFIGS
    if local:
        for soc, (shape, core, _) in SELECTIONS.items():
            if shape == 'simple' and soc not in TEST_CONFIGS:
                yield soc, shape, core


def platform_configs(*, local=False):
    """Select OpenSBI and litmus tests by shape and ISA, never core identity."""
    suites = {'simple': ('opensbi', 'opensbi-smoke-run', 45),
              'tiled4core': ('litmus', 'litmus-smoke-run', 90)}
    return [dict(soc=soc, suite=suites[shape][0], run_target=suites[shape][1], timeout=suites[shape][2],
                 **simulator_options(soc), backend=DEFAULT_BACKEND)
            for soc, shape, _ in software_configs(local)
            if shape in suites and (SELECTIONS[soc][2] == 'rva23' or local and shape == 'simple')]


def linux_configs():
    """Select build-only Linux images by shape and ISA, independently of simulation."""
    return [dict(soc=soc, suite='linux') for soc, shape, _core in SIMULATOR_CONFIGS
            if shape == 'simple' and SELECTIONS[soc][2] == 'rva23']


def simple_software():
    """The local Simple full suite, independent of the smaller hosted enrollment."""
    for soc, (shape, _, isa) in SELECTIONS.items():
        if shape == 'simple':
            yield soc, tuple(s for s in (*NATIVE_SUITES, 'coremark_scalar')
                             if isa == 'rva23' or s not in ('skl', 'highway'))


def native_programs(*, local=False):
    if local:
        for soc, suites in simple_software():
            for suite in suites:
                yield soc, suite
        return
    for suite in (*NATIVE_SUITES, 'coremark_scalar'):
        family = 'coremark' if suite == 'coremark_scalar' else suite
        for soc, shape, _core in SIMULATOR_CONFIGS:
            if family in NATIVE_SOFTWARE.get((shape, SELECTIONS[soc][2]), ()):
                yield soc, suite


# Backend variants reuse the architectural config and software target descriptor.
# Only simulator build/run matrices include this additional backend test.
BACKEND_SMOKE_CONFIGS = (
    ('simple-rv5stage-rva23', 'simple', 'rv5stage'),
    ('simple-rv2wide-rva23', 'simple', 'rv2wide'),
    ('simple-rv3wide-rva23', 'simple', 'rv3wide'),
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
                           if core in ('rv5stage', 'rv2wide', 'rv3wide') and shape in ('mini', 'simple'))


def model_variant(*, trace=False, cosim=False, stats=False):
    """Canonical identity for instrumentation built into the model."""
    mode = 'trace-cosim' if trace and cosim else 'trace' if trace else 'cosim' if cosim else 'normal'
    return mode + ('-stats' if stats else '')


def simulator_options(soc):
    """Keep instrumentation and artifact identity identical for every build and consumer."""
    shape, core, isa = SELECTIONS[soc]
    cosim = soc in COSIM_CONFIGS
    stats = core in ('rv5stage', 'rv2wide', 'rv3wide') and shape in ('simple', 'tiled') and isa == 'rva23'
    return dict(cosim=cosim, stats=stats,
                variant=model_variant(cosim=cosim, stats=stats))


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


def arch_configs(*, local=False):
    return tuple(dict(configuration=key, core=core) for key, shape, core in software_configs(local)
                 if shape == 'simple' and (local or SELECTIONS[key][2] in ('rv32int', 'rv32max', 'rva23')))


# Resource partitioning may depend on implementation speed; test coverage does not.
ARCH_SHARD_COUNTS = {
    'simple-rv5stage-rva23': 16,
    'simple-rv5stage-rv32int': 8,
    'simple-rv5stage-rv32max': 8,
    'simple-rv2wide-rv32int': 8,
    'simple-rv2wide-rv32max': 8,
    'simple-rv2wide-rva23': 16,
    'simple-rv3wide-rv32int': 8,
    'simple-rv3wide-rv32max': 8,
    'simple-rv3wide-rva23': 16,
    'simple-spike-rva23': 8,
    'simple-spike-rv32int': 4,
    'simple-spike-rv32max': 4,
}


def workload_requests(include, *, local=False):
    """Select domain workload requests; GitHub owns jobs, workers, and matrices."""
    programs = [dict(soc=soc, suite=suite, **simulator_options(soc), backend=DEFAULT_BACKEND)
                for soc, suite in sorted(native_programs(local=local)) if include('program', soc, suite)]
    configs = [(*cfg, DEFAULT_BACKEND) for cfg in SIMULATOR_CONFIGS]
    configs.extend((*cfg, backend) for cfg in BACKEND_SMOKE_CONFIGS for backend in BACKEND_SMOKE_VARIANTS)
    builds = []
    for soc, shape, core, backend in configs:
        if include('simulator', soc, backend):
            row = simulation_entry(soc, shape, core, backend, smoke_variant=backend != DEFAULT_BACKEND)
            tests = [t for t in row['software_tests'].split() if include('harness', soc, backend, t)]
            row.update(software_tests=' '.join(tests), run_harness=bool(tests))
            builds.append(row)
    return dict(targets=sorted(soc for soc in SELECTIONS if include('target', soc)),
                programs=programs, simulators=builds,
                # Share firmware archives only on qualified platform targets. Other local
                # OpenSBI checks keep their own target-layout validation at execution.
                platforms=[r for r in platform_configs() if include('platform', r['soc'], r['suite'])],
                linux=[r for r in linux_configs() if include('linux', r['soc'])])
