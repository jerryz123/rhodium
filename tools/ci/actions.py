#!/usr/bin/env python3
# Selects affected Moon tasks and their required evidence, independently of GitHub job topology.
# SPDX-License-Identifier: Apache-2.0

import argparse
from functools import lru_cache
import json
import os
import subprocess

from .policy import ROOT, COREMARK_SUITES
from .catalog import act_model, materialize
from .inputs import gitlinks, pin_file, pin_stamp
from .gate import receipt_id
from .moon import closure, query


@lru_cache(None)
def ci_tasks():
    """Read selection and evidence roles from the expanded Moon graph."""
    tasks = query('query', 'tasks', '--tags', '^ci-(root|receipt)$')['tasks']['rhodium']
    ci = {key.replace('--', ':'): task for key, task in tasks.items()}
    for key, task in ci.items():
        categories = {t for t in task['tags'] if t.startswith('ci-')} - {'ci-root', 'ci-receipt'}
        if categories and categories != {'ci-' + key.partition(':')[0]}:
            raise ValueError('hosted task category and namespace disagree: ' + key)
    return ci


def expected_receipts(selected):
    return sorted(receipt_id(key) for key, task in ci_tasks().items()
                  if key in selected and 'ci-receipt' in task['tags'])


def enrollment():
    """Read selectable tasks and require an enrolled consumer for each fixture group."""
    fixtures = materialize()
    ci_tasks.cache_clear()
    hosted = ci_tasks()
    for name in fixtures:
        owner = hosted.get('check:' + name[len('fixtures--'):], {})
        if '@group(' + name + ')' not in owner.get('inputs', []):
            raise ValueError('fixture group without an enrolled consumer: ' + name)
    return {key for key, task in hosted.items() if 'ci-root' in task['tags']}


def ignored(path):
    return path.endswith('.md') or path in ('LICENSE', 'DCO', 'NOTICE', '.gitignore', '.gitattributes') or path.startswith('LICENSE.')


def select(paths, enrolled):
    if paths is None:
        return enrolled
    paths = {p for p in paths if not ignored(p)}
    if not paths:
        return set()
    global_files = {'.gitmodules', '.github/workflows/ci.yml', 'sims/model_inputs.py',
                    *('tools/ci/' + name + '.py' for name in
                      ('actions', 'catalog', 'inputs', 'moon', 'policy', 'gate', 'receipts'))}
    if any(p in global_files or p.startswith(('.moon/', '.github/actions/setup-moon/')) for p in paths):
        return enrolled
    missing = [p for p in paths if not (ROOT / p).exists()]
    if missing:
        materialize(paths=missing)
    pins = gitlinks()
    # A gitlink affects consumers of its revision and checks that cover its tree.
    probes = {p: {str(pin_stamp(p)), pin_file(p)} if p in pins else {p} for p in paths}
    def affected(files):
        result = query('query', 'affected', '--upstream', 'none', '--downstream', 'deep', data={'files':sorted(files)})
        return {key.split(':')[1].replace('--', ':'): v for key, v in result.get('tasks', {}).items()}

    def owners(tasks, files):
        return {key for key, state in tasks.items() if key != 'check:host-hygiene' and files.intersection(state.get('files', []))}

    tasks = affected(set.union(*probes.values()))
    for path, files in probes.items():
        hardware = (path.startswith(('cores/', 'socs/', 'sims/', 'rhodium/')) and '/tests/' not in path
                    and path.endswith(('.rhdl', '.rhm', '.rkt')))
        direct = owners(tasks, files)
        # Moon reports witness paths rather than an exhaustive list for each
        # task. Resolve ambiguous witnesses individually, without a second glob engine.
        if len(paths) > 1 and (not direct or hardware and all(k.startswith('check:') for k in direct)):
            single = affected(files)
            direct = owners(single, files)
            tasks.update({k:v for k,v in single.items() if k not in tasks})
        if not direct and path.startswith('sims/program-test/performance-baselines/') and (ROOT / path).exists():
            raise ValueError('performance baseline has no enrolled workload: ' + path)
        if not direct or hardware and all(k.startswith('check:') for k in direct):
            return enrolled
    def covered(key):
        if not key.startswith('act:'):
            return key in tasks
        # The runtime needs an attested simulator. Its native writer isn't an
        # ACT input: select the model, ACT ELFs, or a shard's own changed files.
        soc = key.partition(':')[2]
        return ('act-elf:' + soc in tasks or act_model(soc) in tasks or
                any(state.get('files') for k,state in tasks.items() if k.startswith(key + ':shard-')))
    return {k for k in enrolled if covered(k)}


def selection(selected):
    """Expose graph facts, not runners, matrices, or GitHub dependency edges."""
    tags = {}
    for key, task in ci_tasks().items():
        if key in selected:
            for tag in task['tags']:
                tags.setdefault(tag, []).append(key)
    return dict(tasks=sorted(selected), tags={k: sorted(v) for k, v in sorted(tags.items())},
                endpoints=expected_receipts(selected))


def revision_pins(revision):
    """Compare indexed submodule paths and revisions without entering their checkouts."""
    tree = subprocess.check_output(['git', 'ls-tree', '-rz', revision], cwd=ROOT)
    return {entry for entry in tree.split(b'\0') if entry.startswith(b'160000 ')}


def changed_paths(base_revision, head_revision):
    """Select changes between the two supplied trees, never the local checkout."""
    if not all(r and set(r) != {'0'} for r in (base_revision, head_revision)):
        return None
    ancestry = subprocess.run(['git', 'merge-base', '--is-ancestor', base_revision, head_revision],
                              cwd=ROOT, capture_output=True).returncode
    if ancestry not in (0, 1):
        return None
    try:
        if ancestry == 0 and revision_pins(base_revision) == revision_pins(head_revision):
            return query('query', 'changed-files', '--base', base_revision, '--head', head_revision)['files']
        # Moon uses the merge base and may expand changed submodules into their checked-out
        # contents. In either exceptional case compare the two superproject trees directly.
        files = subprocess.check_output(['git', 'diff', '--no-renames', '--ignore-submodules=none',
                                         '--name-only', '-z', base_revision, head_revision, '--'], cwd=ROOT)
        return [os.fsdecode(p) for p in files.split(b'\0') if p]
    except subprocess.CalledProcessError:
        return None


def main():
    parser = argparse.ArgumentParser()
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument('--all', action='store_true')
    src.add_argument('--coremark', action='store_true')
    src.add_argument('--paths', nargs='*')
    src.add_argument('--revisions', nargs=2)
    parser.add_argument('--pretty', action='store_true')
    args = parser.parse_args()
    enrolled = enrollment()
    roots = ({k for k in enrolled if k.startswith('program:') and k.rpartition(':')[2] in COREMARK_SUITES}
             if args.coremark else select(changed_paths(*args.revisions) if args.revisions else args.paths, enrolled))
    selected = closure(roots)
    result = selection(selected)
    print(json.dumps(result, indent=2 if args.pretty else None, separators=None if args.pretty else (',', ':')))


if __name__ == '__main__':
    main()
