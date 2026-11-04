# Builds Moon file groups from scoped sources, fixture inventories, build recipes, and indexed submodules.
# SPDX-License-Identifier: Apache-2.0
from functools import lru_cache
import os
from pathlib import Path
import subprocess
import tempfile

from sims.model_inputs import INSTRUMENTATION, group_specs, patterns
from tools.testing.rsim.run import inventory as rsim_inventory
from .policy import ROOT, SELECTIONS


def scoped_groups(specs, paths=()):
    """Use Git pathspecs to enumerate scoped inputs, including deleted diff paths."""
    def listed(args, env=None):
        data = subprocess.check_output(['git', 'ls-files', '-z', *args], cwd=ROOT, env=env)
        return {os.fsdecode(p) for p in data.split(b'\0') if p}

    # A commit diff may name files absent from the checkout. Match these with
    # the same Git pathspecs in an isolated index; never touch the real index.
    staged = subprocess.check_output(['git', 'diff', '--cached', '--no-renames', '--name-only',
                                      '--diff-filter=D', '-z', 'HEAD'], cwd=ROOT)
    missing = sorted({p for p in paths if not (ROOT / p).exists()} |
                     {os.fsdecode(p) for p in staged.split(b'\0') if p})
    # Collapse wholly owned implementation subtrees back to native moon globs.
    # Only compact beneath a declared recursive root, never an exact-file rule.
    tree, leaves = {}, {}
    for path in listed(['--cached', '--others', '--exclude-standard']) | set(missing):
        leaves.setdefault(path.rpartition('/')[0], set()).add(path)
        parent = path
        while '/' in parent:
            parent = parent.rsplit('/', 1)[0]
            tree.setdefault(parent, set()).add(path)
    with tempfile.TemporaryDirectory() as temp:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(temp) / 'index'))
        if missing:
            empty = subprocess.check_output(['git','hash-object','--stdin'], input=b'', cwd=ROOT).strip()
            subprocess.run(['git', '-c', 'core.splitIndex=false', 'update-index', '-z', '--index-info'], cwd=ROOT, env=env, check=True,
                           input=b''.join(b'100644 ' + empty + b'\t' + os.fsencode(p) + b'\0' for p in missing))
        @lru_cache(None)
        def matching(patterns):
            # Match positives and negatives separately: Git strips a common
            # path prefix when optimizing a mixed positive/negative pathspec.
            if not patterns:
                return set()
            args = ['--', *(':(glob)' + p for p in patterns)]
            found = listed(['--cached', '--others', '--exclude-standard', *args])
            return found | listed(args, env) if missing else found

        result = {}
        for group, patterns in specs.items():
            files = matching(tuple(p for p in patterns if not p.startswith('!'))) - matching(
                tuple(p[1:] for p in patterns if p.startswith('!')))
            roots = [p[:-5] for p in patterns if p.endswith('/**/*') and not p.startswith('!')
                     and '*' not in p[:-5]]
            globs = []
            for folder in sorted(tree, key=lambda p: (p.count('/'), p)):
                if any(folder == root or folder.startswith(root + '/') for root in roots) and tree[folder] <= files:
                    globs.append(folder + '/**/*')
                    files -= tree[folder]
            for folder, direct in leaves.items():
                if direct <= files and any(folder == r or folder.startswith(r + '/') for r in roots):
                    globs.append(folder + '/*')
                    files -= direct
            result[group] = sorted([*globs, *files])
        return result


def group(*names):
    return ['@group(' + name + ')' for name in names]


# Common recipe inputs, also published individually for consumers that need a subset.
BUILD_TOOLS = {
    'compiler': ('tools/install-riscv-*.sh', 'tools/build/paths.*', 'riscv/gnu-toolchain.rhm', '.github/actions/setup-riscv-toolchain/**/*'),
    'elf': ('sw/build/build.py', 'sw/build/program_target.py'),
    'patches': ('riscv/patched_submodule.py',),
    'build-driver': ('.moon/tasks/software.yml', 'sw/build/moon-env.sh'),
    'isa-source': ('sw/riscv-isa-tests', 'sw/riscv-isa-tests/**/*'),
}


SAIL = ('sail', 'patches', 'pin--riscv--sail-riscv')
RUNTIME = ('harness-runtime', 'fesvr-runtime', 'verilator')
RUNNER = ('native-runner', 'bundle', 'evidence', 'simulator-artifact', 'build-groups')
ACT_TOOLS = ('act-tools', 'patches', 'pin--sw--riscv-arch-test')


SOURCES = {'isa': 'riscv-isa-tests', 'benchmark': 'riscv-isa-tests', 'coremark': 'coremark',
           'coremark_scalar': 'coremark', 'embench': 'embench-iot', 'bringup': 'bringup-bench',
           'skl': 'skl', 'highway': 'highway', 'opensbi': 'opensbi', 'linux': 'buildroot', 'litmus': 'litmus-tests-riscv'}


def recipe(suite):
    """Return the input groups for this suite's build recipe."""
    return ('build--' + ('coremark' if suite == 'coremark_scalar' else suite),)


def product_groups(pins):
    """Augment the native recipe file groups with their shared tools and indexed revisions."""
    groups = dict(BUILD_TOOLS)

    def pin(source):
        return pins['pin--sw--' + source]

    # inputs.yml gives both groups the same library sources. Both use the same
    # patch helper and pin; only runtime consumers need the full bridge directory.
    groups['fesvr-library'] = [*BUILD_TOOLS['patches'], *pins['pin--riscv--riscv-isa-sim']]
    groups['fesvr-runtime'] = [*groups['fesvr-library'], 'sims/fesvr/**/*']
    for suite, source in SOURCES.items():
        if suite in ('coremark_scalar', 'linux'):
            continue
        inputs = [*BUILD_TOOLS['compiler'], *BUILD_TOOLS['elf'], *pin(source)]
        if suite != 'opensbi':
            inputs += BUILD_TOOLS['build-driver']
        if suite not in ('isa', 'coremark', 'highway'):
            inputs += BUILD_TOOLS['patches']
        if suite in ('isa', 'benchmark', 'litmus'):
            inputs += (*BUILD_TOOLS['isa-source'], *pin('riscv-isa-tests'))
        groups['build--' + suite] = sorted(set(inputs))
    # OpenSBI is also built into the Linux image. Include its complete recipe
    # in both products, even when only the Linux adapter is run.
    groups['build--opensbi'].extend(('sw/build/opensbi.py', 'sw/opensbi', 'sw/opensbi/**/*',
                                     'sw/opensbi-patches/**/*', 'sims/opensbi/**/*'))
    groups['build--linux'] = [*groups['build--opensbi'], *pin(SOURCES['linux'])]
    return groups


# These keys reuse generated recipe inputs; fixed keys live in cache-keys.yml.
RECIPE_CACHE_KEYS = {
    'act-tools': ACT_TOOLS, 'act-source': ('act-source', *ACT_TOOLS, *SAIL, 'compiler'),
    'fesvr-library': ('fesvr-library',),
    **{'build:' + suite: recipe(suite) for suite in ('linux', 'litmus', 'opensbi')},
}


def gitlinks():
    index = subprocess.check_output(['git', 'ls-files', '--stage', '-z'], cwd=ROOT)
    return {path:meta.split()[1] for record in index.split(b'\0') if record.startswith(b'160000 ')
            for meta,path in [record.decode().split('\t', 1)]}


def pin_stamp(path):
    return Path('.rhodium-cache/moon/pins') / path


def pin_file(path):
    # Probe directory globs independently of whether the submodule is populated.
    # Hash consumers use the real indexed-revision stamp instead.
    return path + '/rhodium-gitlink.revision'


def fixture_groups():
    # Fixture manifests own their sources. Static checks depend on these groups
    # instead of duplicating their changing inventory in YAML.
    fixtures = {}
    for row in rsim_inventory(check_files=False):
        group = row['group']
        group = 'std' if group == 'language' else group.replace('cores-', 'core-')
        fixtures.setdefault('rsim-' + group, set()).update([row['source'], row['driver'], *row['sources']])
    listing = subprocess.check_output(['bash', 'tools/testing/circt/run.sh', '--list-inputs'], cwd=ROOT, text=True)
    for line in listing.splitlines():
        group, path = line.split('\t')
        fixtures.setdefault('circt-' + group.replace('cores-', 'core-'), set()).add(path)
    for key, paths in sorted(fixtures.items()):
        core_groups = []
        if key.startswith('rsim-core-'):
            for core in ('rv2wide', 'rv3wide', 'rv5stage', 'spike'):
                if any(p.startswith(f'cores/{core}/') for p in paths):
                    core_groups.append('core--' + core)
                    paths.add(f'cores/{core}/tests/*')
            if any(p.startswith('cores/superscalar/') for p in paths):
                core_groups.extend(('core--rv2wide', 'core--rv3wide'))
                paths.add('cores/superscalar/tests/*')
        yield key, sorted(paths), core_groups


def catalog_groups(paths=()):
    specs = {}
    for shape, core, _ in set(SELECTIONS.values()):
        specs.update(group_specs(shape, core))
    groups = scoped_groups(specs, paths)
    groups.update(INSTRUMENTATION)
    for key, fixtures, cores in fixture_groups():
        groups['fixtures--' + key] = sorted({g for p in fixtures for g in patterns(p)} |
                                            {p for core in cores for p in groups[core]})
    # A gitlink is not a directory's content hash. Its indexed revision must
    # enter the consumer's key even before the submodule is initialized.
    pins = {}
    for path, revision in gitlinks().items():
        stamp = pin_stamp(path)
        full = ROOT / stamp
        full.parent.mkdir(parents=True, exist_ok=True)
        if not full.exists() or full.read_text() != revision:
            with tempfile.NamedTemporaryFile(dir=full.parent, mode='w', delete=False) as f:
                f.write(revision)
            os.replace(f.name, full)
        pins['pin--' + path.replace('/', '--')] = [str(stamp)]
    groups.update(pins)
    groups.update(product_groups(pins))
    return groups
