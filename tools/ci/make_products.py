# Imports the selected Make invocation's concrete file and phony prerequisites for local queries.
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import os
import subprocess

from .policy import CHECKS, ROOT, SELECTIONS, model_variant


def handoffs(directory, soc, backend, trace, cosim, stats):
    """Products consumed across recipe boundaries (not expressible as Make file prerequisites)."""
    if directory == Path('.'):
        return {**{c.target: 'check:' + c.key for c in CHECKS},
                'formal-test': 'formal', 'formal-differential-test': 'formal-differential', 'examples-formal': 'formal',
                'emacs-test': 'emacs', 'update-verilog-goldens': 'goldens', 'verilog-golden-test': 'goldens',
                'sram-test': 'sram'}
    if directory == Path('sram'):
        return {'plugin': 'sram', 'test': 'sram'}
    if directory == Path('rheg/perfetto'):
        return {'libraries': 'trace-runtime', 'test': 'trace-runtime'}
    if directory == Path('vlsi/sim'):
        return {'mapping': 'memory-map', 'smoke': 'mapped-smoke'}
    if directory == Path('vlsi'):
        return {'rtl': 'wrapper-rtl', 'harden': 'harden', 'harden-native': 'harden', 'lvs-smoke': 'lvs',
                'lvs-smoke-native': 'lvs', 'gds': 'gds', 'mini-rv5stage-soc-memory-map': 'memory-map',
                'mini-rv5stage-soc-synth': 'synthesis', 'mini-rv5stage-soc-synth-native': 'synthesis'}
    if directory == Path('sims'):
        variant = model_variant(trace=trace, cosim=cosim, stats=stats)
        model = f'model:{soc}:{backend}:{variant}'
        return {'simulator': model, 'run': model, 'program-target': 'target:' + soc,
                'simulator-attestation': 'simulator:all', 'arch-test-setup': 'patches',
                'arch-test-elfs': 'act-source', 'sail-cosim-build': 'cosim', 'arch-test-sail-setup': 'sail',
                **{target: 'build:' + suite for target, suite in {
                    'isa-smoke-elfs': 'isa', 'isa-elfs': 'isa', 'benchmark-elfs': 'benchmark',
                    'tiled-mt-benchmark-elfs': 'benchmark', 'coremark-elf': 'coremark',
                    'coremark_scalar-elf': 'coremark', 'embench-elfs': 'embench', 'bringup-elfs': 'bringup',
                    'skl-elfs': 'skl', 'highway-elfs': 'highway', 'litmus-elfs': 'litmus',
                    'litmus-smoke-elfs': 'litmus', 'litmus-full-elfs': 'litmus', 'linux-elfs': 'linux',
                    'opensbi-elfs': 'opensbi'}.items()}}
    return {}


def include_make(graph, directory, soc, backend, trace, cosim, stats):
    """Query GNU Make, not makefile syntax. Do not run recipes or probe a real build goal."""
    cwd = (ROOT / directory).resolve()
    relative = cwd.relative_to(ROOT)
    makefile = str(relative / 'Makefile')
    subprocess.run(['git', '-C', str(ROOT), 'ls-files', '--error-unmatch', '--', makefile],
                   check=True, stdout=subprocess.DEVNULL)
    args = ['make', '-rqp', '-f', 'Makefile', '-f', '-', 'rhodium-inventory']
    if relative == Path('sims'):
        if soc not in SELECTIONS:
            raise ValueError(f'unknown SoC: {soc}')
        args.extend([f'SOC={soc}', f'RTL_BACKEND={backend}', f'TRACE={int(trace)}',
                     f'COSIM={int(cosim)}', f'STATS={int(stats)}'])
    data = subprocess.run(args, cwd=cwd, env=dict(os.environ, LC_ALL='C', MAKEFLAGS='', MFLAGS=''),
                          input='rhodium-inventory: ;\n', text=True, capture_output=True)
    if data.returncode not in (0, 1):
        raise ValueError(data.stderr.strip() or 'GNU Make inventory failed')
    _, start, rules = data.stdout.partition('# Files\n')
    rules, end, _ = rules.partition('# files hash-table stats:')
    if not start or not end:
        raise ValueError('expected the GNU Make files database')
    entries = {}
    for line in rules.splitlines():
        if not line or line.startswith(('#', '\t', ' ')):
            continue
        name, colon, rest = line.partition(':')
        if not colon or name in ('.PHONY', 'rhodium-inventory'):
            continue
        normal, _, ordered = rest.partition('|')
        entries[name] = (normal.split(), ordered.split())
    # Leaf prerequisites can live outside the checkout (toolchain, build dir).
    # Retain their names but do not treat them as tracked source inputs.
    for normal, ordered in list(entries.values()):
        for dep in (*normal, *ordered):
            entries.setdefault(dep, ([], []))
    bindings = handoffs(relative, soc, backend, trace, cosim, stats)
    for name, (normal, ordered) in entries.items():
        file = (cwd / name).resolve()
        inputs = (file.relative_to(ROOT).as_posix(),) if file.is_relative_to(ROOT) else ()
        deps = ['make:' + k for k in normal]
        if name in bindings:
            deps.append(bindings[name])
        graph.add('make:' + name, inputs=inputs, requires=deps,
                  after=['make:' + k for k in ordered])
    return graph.finish()
