# Declares inputs to the host configuration snapshot shared by native descriptors and ACT.
# SPDX-License-Identifier: Apache-2.0
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NATIVE_WRITER_INPUTS = ('sims/program-test/write-target.rhm', 'riscv/gnu-toolchain.rhm', 'sims/config.mk')

# Modules that supply values to config_metadata, description, DTS or UDB.
# A mixed config/RTL module is intentionally a whole-file input. Do not follow
# circuit factories into the datapaths they instantiate during RTL elaboration.
COMMON = (
    'socs/configs/inputs.py',
    'socs/configs/selection*', 'socs/configs/resolve.rhm', 'socs/configs/metadata*',
    'socs/configs/isa-profiles.rhm', 'socs/harts/implementation.rhdl',
    'socs/platform/description.rhm', 'socs/platform/endpoint-params.rhdl',
    'socs/platform/fabric-profiles.rhm', 'socs/platform/boot.rhdl', 'socs/platform/peripherals.rhdl',
    'cores/chi-hart.rhdl', 'devicetree/*.rhm', 'riscv/isa/*.rhm', 'riscv/model/*.rhm',
    'riscv/udb*.rhm', 'riscv/rtl/pma.rhdl', 'support/*.rhm',
    'rhodium/main.rkt', 'rhodium/language.rhm', 'rhodium/core/*.rhm', 'rhodium/std/bits.rhdl',
    'rhodium/std/interconnect.rhdl', 'rhodium/base/**/*.rkt', 'rhodium/base/**/*.rhm',
    'rhodium/frontend/**/*.rhm', 'rhodium/frontend/**/*.rkt',
    'chi/protocol/*.rhdl', 'chi/home/home-common.rhdl', 'chi/home/home.rhdl',
    'devices/boot/*.rhm', 'devices/boot/*.rhdl', 'devices/interrupt/*.rhm',
    'devices/interrupt/aclint.rhdl', 'devices/interrupt/plic.rhdl', 'devices/uart/uart16550.rhdl',
)
ONE_HART = ('socs/one-hart/system.rhdl', 'chi/home/inclusive-home.rhdl')
SHAPES = {
    'mini': (*ONE_HART, 'socs/mini-soc/main.rhdl', 'chi/subordinate/memory-controller.rhdl'),
    'simple': (*ONE_HART, 'socs/single-core-soc/main.rhdl'),
    'tiled': ('socs/tiled-soc/main.rhdl', 'socs/tiled-soc/layout.rhm', 'noc/std/topology/rectangular-mesh.rhm'),
}
RTL_PROFILE = ('cores/profile.rhm', 'cores/udb.rhm', 'cores/cache/config.rhm', 'cores/csr/config.rhm',
               'cores/vector/profile.rhm', 'cores/fp/profile.rhm', 'cores/cache/udb.rhm', 'cores/csr/udb.rhm',
               'cores/vector/udb.rhm', 'cores/mmu/udb.rhm', 'cores/cache/chi/attachment.rhdl',
               'cores/cache/chi/flits.rhdl', 'chi/transactions/retryable-transaction.rhdl',
               'chi/transactions/cache-maintenance.rhdl')
CORES = {
    'rv5stage': (*RTL_PROFILE, 'socs/configs/core-profiles.rhm', 'cores/rv5stage/chi/foundation.rhdl'),
    'rv2wide': (*RTL_PROFILE, 'socs/configs/rv2wide-core-profile.rhm',
               'cores/superscalar/profile.rhm', 'cores/superscalar/udb.rhm'),
    'rv3wide': (*RTL_PROFILE, 'socs/configs/rv3wide-core-profile.rhm',
               'cores/superscalar/profile.rhm', 'cores/superscalar/udb.rhm'),
    'spike': ('socs/configs/spike-core-profile.rhm', 'cores/spike/chi/foundation.rhdl',
              'chi/transactions/retryable-transaction.rhdl'),
}


def groups(shape, core):
    family = 'tiled' if shape in ('tiled4core', 'tiled8core') else shape
    return {'common': COMMON, 'shape-' + family: SHAPES[family],
            'core-' + core: (*CORES[core], f'socs/harts/{core}.rhdl',
                             f'cores/{core}/profile.rhm', f'cores/{core}/udb.rhm')}


def main():
    parser = argparse.ArgumentParser(description='Print native descriptor prerequisites for GNU Make.')
    parser.add_argument('shape')
    parser.add_argument('core', choices=CORES)
    args = parser.parse_args()
    patterns = [pattern for group in groups(args.shape, args.core).values() for pattern in group]
    # Keep literal prerequisites even if missing: Make should diagnose a
    # removed input rather than declare an old descriptor up to date.
    files = {p for pattern in (*patterns, *NATIVE_WRITER_INPUTS)
             for p in (ROOT.glob(pattern) if '*' in pattern else [ROOT / pattern])
             if 'tests' not in p.relative_to(ROOT).parts and not p.is_dir()}
    print(' '.join(str(p) for p in sorted(files)))


if __name__ == '__main__':
    main()
