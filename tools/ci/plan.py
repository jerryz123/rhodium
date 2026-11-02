#!/usr/bin/env python3
# Projects the repository product graph onto the enrolled GitHub Actions matrix.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import subprocess
from functools import lru_cache

from .policy import (BACKEND_SMOKE_CONFIGS, BACKEND_SMOKE_VARIANTS, DEFAULT_BACKEND,
                     ROOT, SHORT_PROGRAM_SUITES, SIMULATOR_CONFIGS, SINGLE_CORE_SOCS, arch_configs,
                     arch_shards, check_matrix, linux_configs, platform_configs, simulation_entry,
                     simulator_options)
from .software import ADAPTERS
from .products import build_graph


@lru_cache(maxsize=1)
def catalog():
    return build_graph()


def ignored(path):
    return path.endswith('.md') or path in ('LICENSE', 'DCO', 'NOTICE', '.gitignore', '.gitattributes') or path.startswith('LICENSE.')


def impact(path):
    graph = catalog()
    if ignored(path):
        return {}, False
    changes = graph.affected(path)
    # An extension-based hygiene match is not an ownership declaration.
    unknown = not changes.keys() - {'check:host-hygiene'}
    # Check policy for a whole directory must not hide an unmapped hardware
    # implementation (e.g. a new backend family). Fail closed until enrolled.
    if (path.startswith(('cores/', 'socs/', 'sims/', 'rhodium/')) and '/tests/' not in path
            and path.endswith(('.rhdl', '.rhm', '.rkt')) and all(k.startswith('check:') for k in changes)):
        unknown = True
    global_input = path in ('Makefile', '.gitmodules', '.github/workflows/ci.yml')
    if path.startswith('sims/program-test/performance-baselines/') and not changes:
        raise ValueError(f'performance baseline has no enrolled workload: {path}')
    return changes, unknown or global_input


def plan_for_paths(paths):
    graph = catalog()
    roots = set()
    for path in paths:
        changes, full = impact(path)
        if full:
            roots = graph.enrolled
            break
        roots.update(graph.enrolled.intersection(changes))
    return project(graph.prerequisites(roots))


def project(selected):
    def socs(kind):
        return {k[len(kind)+1:] for k in selected if k.startswith(kind + ':')}
    checks = socs('check')
    matrix = check_matrix(checks)
    programs = sorted(key.split(':') for key in socs('program'))
    suites = {'include': [dict(soc=soc, suite=suite, **simulator_options(soc), backend=DEFAULT_BACKEND)
                          for soc, suite in programs]}
    platforms = {'include': [r for r in platform_configs() if f"platform:{r['soc']}:{r['suite']}" in selected]}
    arch_builds = {r['configuration']: r for r in arch_configs() if r['configuration'] in socs('act')}
    arch_runs = [r for r in arch_shards() if r['configuration'] in arch_builds]
    configs = [(*config, DEFAULT_BACKEND) for config in SIMULATOR_CONFIGS]
    configs.extend((*config, backend) for config in BACKEND_SMOKE_CONFIGS for backend in BACKEND_SMOKE_VARIANTS)
    builds = []
    for soc, shape, core, backend in configs:
        if f'simulator:{soc}:{backend}' not in selected:
            continue
        row = simulation_entry(soc, shape, core, backend, smoke_variant=backend != DEFAULT_BACKEND)
        tests = [t for t in row['software_tests'].split() if f'harness:{soc}:{backend}:{t}' in selected]
        row.update(software_tests=' '.join(tests), run_harness=bool(tests),
                   run_mapped=soc == 'mini-rv5stage-rva23' and 'mapped-smoke' in selected)
        builds.append(row)
    linux = {'include': [r for r in linux_configs() if r['soc'] in socs('linux')]}
    adapters = socs('adapter')
    native = sorted(socs('native-check'))
    native_inputs = catalog().prerequisites('native-check:' + t for t in native)
    run_checks, run_program_native, run_simulator, run_targets = map(bool, (checks, programs, builds, socs('target')))
    harness_build_suites = sorted(s for s in ('isa', 'benchmark') if 'harness:' + s in selected)
    # Matching-row needs are not supported by Actions matrices. Configs
    # with downstream execution use named jobs; build-only rows stay batched.
    pipelines = (set(SINGLE_CORE_SOCS) | {row['soc'] for row in platform_configs()}
                 | {soc for soc, shape, _core in SIMULATOR_CONFIGS if shape == 'tiled8core'})
    config_jobs = {}
    groups = {key: {'include': []} for key in ('plain', 'cosim', 'plain-elf', 'cosim-elf')}
    for build in builds:
        if build['backend'] == DEFAULT_BACKEND and build['soc'] in pipelines:
            soc = build['soc']
            config_jobs[soc] = dict(simulator=build,
                                   program_groups=list(dict.fromkeys('short' if row['suite'] in SHORT_PROGRAM_SUITES else row['suite']
                                                                     for row in suites['include'] if row['soc'] == soc)),
                                   arch=soc in arch_builds,
                                   arch_config=arch_builds.get(soc),
                                   arch_matrix={'include': [row for row in arch_runs if row['configuration'] == soc]},
                                   platform=any(row['soc'] == soc for row in platforms['include']))
        else:
            shared_elf = any(target in build.get('software_tests', '').split()
                             for target in ('isa-smoke', 'tiled-mt-benchmark-test'))
            key = ('cosim' if build['cosim'] else 'plain') + ('-elf' if shared_elf else '')
            groups[key]['include'].append(build)
    return {
        "run_compile": 'bytecode' in selected,
        "run_sail": 'sail' in selected,
        "run_cosim": any(build["cosim"] for build in builds),
        "native_checks": native,
        "native_setup": sorted(native_inputs & {'bytecode', 'native-toolchain', 'fesvr', 'verilator', 'circt-tool'}),
        "run_native": bool(native),
        "software_checks": sorted(ADAPTERS[name][0] for name in adapters),
        "software_setup": sorted({setup for name in adapters for setup in ADAPTERS[name][1]}),
        "run_checks": run_checks,
        "checks_matrix": matrix,
        "run_simulator": run_simulator,
        "run_targets": run_targets,
        "target_configs": sorted(socs('target')),
        "run_linux_build": bool(linux['include']),
        "linux_matrix": linux,
        "simulator_matrix": {"include": builds},
        "harness_build_suites": harness_build_suites,
        "platform_matrix": platforms,
        "platform_build_suites": sorted({row['suite'] for row in platforms['include']}),
        "run_program_native": run_program_native,
        "program_matrix": suites,
        "run_program_arch": bool(arch_builds),
        "config_jobs": config_jobs,
        "simulator_groups": groups,
        "program_build_groups": list(dict.fromkeys('short' if row['suite'] in SHORT_PROGRAM_SUITES and row['suite'] != 'isa' else row['suite']
                                                   for row in suites['include'])),
    }


def changed_paths(base_revision, head_revision):
    for revision in (base_revision, head_revision):
        if not revision or set(revision) == {"0"}:
            return None
        if subprocess.run(["git", "cat-file", "-e", f"{revision}^{{commit}}"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            return None
    result = subprocess.run(["git", "diff", "--no-renames", "--name-only", "-z", base_revision, head_revision], text=True, capture_output=True)
    return list(filter(None, result.stdout.split('\0'))) if result.returncode == 0 else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument("--all", action="store_true", help="select all enrolled CI products")
    src.add_argument("--paths", nargs="*", help="select CI work for repository-relative paths")
    src.add_argument("--revisions", nargs=2, metavar=("BASE", "HEAD"))
    src.add_argument("--products", action="store_true", help="list enrolled and local products")
    src.add_argument("--requires", metavar="PRODUCT", help="show direct and transitive prerequisites")
    src.add_argument("--fingerprint", metavar="PRODUCT", help="hash the product's tracked build inputs")
    src.add_argument("--affected", metavar="PATH", help="explain affected products, including local flows")
    parser.add_argument("--make-dir", help="include this first-party GNU Make build's concrete targets in a local query")
    parser.add_argument("--soc", default="simple-rv5stage-rva23")
    parser.add_argument("--backend", choices=(DEFAULT_BACKEND, *BACKEND_SMOKE_VARIANTS), default=DEFAULT_BACKEND)
    for flag in ('trace', 'cosim', 'stats'):
        parser.add_argument('--' + flag, action='store_true')
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()
    graph = catalog()
    if args.make_dir:
        if not (args.products or args.requires or args.affected):
            parser.error("--make-dir is for local product queries, not CI enrollment")
        from .make_products import include_make
        graph = include_make(build_graph(), args.make_dir, args.soc, args.backend, args.trace, args.cosim, args.stats)
    if args.products:
        plan = {key: {'ci': key in graph.enrolled} for key in sorted(graph.nodes)}
    elif args.fingerprint:
        if args.fingerprint not in graph.nodes:
            parser.error("unknown product: " + args.fingerprint)
        print(graph.fingerprint(ROOT, args.fingerprint))
        return
    elif args.requires:
        if args.requires not in graph.nodes:
            parser.error("unknown product: " + args.requires)
        node = graph.nodes[args.requires]
        plan = dict(inputs=node.inputs, covers=node.covers, excludes=node.excludes, direct=node.requires, after=node.after,
                    transitive=sorted(graph.prerequisites([args.requires]) - {args.requires}))
    elif args.affected:
        parents, full = impact(args.affected)
        if args.make_dir and not ignored(args.affected):
            parents = graph.affected(args.affected)
        def chain(key):
            return [*chain(parents[key]), key] if parents[key] is not None else [args.affected, key]
        plan = dict(full_ci_fallback=full, affected={key: chain(key) for key in sorted(parents)})
    else:
        paths = changed_paths(*args.revisions) if args.revisions else args.paths
        plan = project(graph.prerequisites(graph.enrolled)) if paths is None else plan_for_paths(paths)
    print(json.dumps(plan, indent=2 if args.pretty else None, separators=None if args.pretty else (",", ":")))


if __name__ == "__main__":
    main()
