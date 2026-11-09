# Defines the scoped RTL emission inputs used by configured Moon simulator stages.
# SPDX-License-Identifier: Apache-2.0

from socs.configs.inputs import groups as config_groups

# Keep exclusions inside their owner. Moon flattens negative globs across all
# task inputs; a shared-core exclusion must not mask a core or fixture input.
IMPLEMENTATION = {
    'language': [
        'rhodium/core/**/*', 'rhodium/base/**/*', 'rhodium/lowering/**/*', 'rhodium/analysis/**/*',
        'rhodium/frontend/**/*', 'rhodium/main.rkt', 'rhodium/language.rhm',
    ],
    'library': ['rhodium/std/**/*', 'flow/**/*', 'support/**/*'],
    'compilation': ['rhodium/compile/**/*', 'rhodium/event/**/*', 'rhodium/diagram/**/*'],
    'backend--circt': ['rhodium/backend/circt*', 'rhodium/backend/circt*/**/*'],
    'backend--verilog': ['rhodium/backend/verilog*', 'rhodium/backend/verilog*/**/*'],
    'backend--rsim': [
        'rhodium/backend/rsim*', 'rhodium/backend/rsim*/**/*', 'rhodium/backend/verilog*',
        'rhodium/backend/verilog*/**/*',
    ],
    'architecture': [
        'riscv/isa/**/*', 'riscv/model/**/*', 'riscv/rtl/**/*', 'riscv/udb*', 'riscv/udb*/**/*',
        'cores/common/profile.rhm', 'cores/common/udb.rhm', 'devicetree/**/*',
    ],
    'soc-config': [
        'socs/configs/**/*', 'socs/platform/**/*', 'socs/harts/implementation.rhdl',
        'sims/test-config*', 'sims/test-config*/**/*', '!socs/configs/core-profiles.rhm',
        '!socs/configs/rv2wide-core-profile.rhm', '!socs/configs/rv3wide-core-profile.rhm',
        '!socs/configs/spike-core-profile.rhm',
    ],
    'soc-hardware': [
        'chi/**/*', 'devices/**/*', 'noc/analysis/**/*', 'noc/authoring/**/*', 'noc/language/**/*',
        'noc/model/**/*', 'noc/plan/**/*', 'noc/std/**/*', 'noc/rtl/**/*',
        '!noc/rtl/wormhole-router.rhdl',
    ],
    'hart-interface': ['cores/common/chi-hart.rhdl', 'cores/common/cosim-source.rhm'],
    'shared-core': ['cores/common/**/*', 'hardfloat/**/*'],
    'core--rv2wide': ['cores/rv2wide/**/*', 'cores/superscalar/**/*', 'socs/harts/rv2wide.rhdl'],
    'core--rv3wide': ['cores/rv3wide/**/*', 'cores/superscalar/**/*', 'socs/harts/rv3wide.rhdl'],
    'core--rv5stage': ['cores/rv5stage/**/*', 'socs/harts/rv5stage.rhdl'],
    'core--spike': ['cores/spike/**/*', 'socs/harts/spike.rhdl'],
    'shape--mini': ['socs/mini-soc/**/*', 'socs/one-hart/**/*'],
    'shape--simple': ['socs/single-core-soc/**/*', 'socs/one-hart/**/*'],
    'shape--tiled': ['socs/tiled-soc/**/*'],
}


def patterns(path):
    parts = ['**' if p == '*' else p for p in path.split('/')]
    if parts[-1] == '**':
        parts.append('*')
    elif len(parts) == 1 and '*' in path:
        parts.insert(0, '**')
    pattern = '/'.join(parts)
    return [pattern, pattern + '/**/*'] if path.endswith('*') and not path.endswith('/*') else [pattern]


EXCLUSIONS = ('!**/tests/**/*', '!**/check-boundaries.sh')
INSTRUMENTATION = {'cosim': ['sims/cosim/**/*'], 'statistics': ['sims/statistics/**/*', 'rhodium/statistics/**/*']}


def group_specs(shape, core):
    specs = {k: [*v, *EXCLUSIONS] for k, v in IMPLEMENTATION.items()}
    specs.update({'config-' + k: [*(g for p in v for g in patterns(p)), *EXCLUSIONS]
                  for k, v in config_groups(shape, core).items()})
    return specs


def emission_inputs(shape, core, backend, *, cosim=False, stats=False):
    """Return literal files and named ownership groups for one hardware emission."""
    family = 'tiled' if shape in ('tiled4core', 'tiled8core') else shape
    harness = {'simple': 'single-core', 'mini': 'mini', 'tiled': 'tiled'}[family]
    files = ['sims/model_inputs.py', 'socs/configs/inputs.py', 'socs/configs/selection.py',
             'sims/emit-soc-harness.rhm', 'sims/fesvr/direct-memory-htif.rhdl',
             'socs/configs/selections.tsv', f'sims/{harness}-soc-harness.rhdl']
    groups = [*('config-' + k for k in config_groups(shape, core)), 'language', 'library', 'architecture',
              'soc-config', 'soc-hardware', 'hart-interface', 'shape--' + family, 'core--' + core,
              'compilation', 'backend--' + backend]
    if core != 'spike':
        groups.append('shared-core')
    if cosim:
        groups.append('cosim')
    if stats:
        groups.append('statistics')
    return files, groups
