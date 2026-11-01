#!/usr/bin/env python3
# Reuses checksum-verified simulator output while leaving verdicts to the current runners.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MARKER = 'RHODIUM-EXECUTION: '


def digest(path):
    with Path(path).open('rb') as stream:
        result = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
        return result.hexdigest()


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def stamp(path):
    stat = Path(path).stat()
    return [stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns]


def libraries(binary):
    """Hash the resolved native dependency closure, not its randomized load addresses."""
    if platform.system() == 'Linux':
        probe = subprocess.run(['ldd', str(binary)], capture_output=True, text=True, check=False)
        text = probe.stdout + probe.stderr
        if 'not found' in text:
            raise ValueError('unresolved simulator library')
        if probe.returncode and not any(s in text for s in ('not a dynamic executable', 'statically linked')):
            raise ValueError('cannot identify simulator libraries')
        paths = set(re.findall(r'(?:=>\s*|^\s*)(/\S+)\s+\(', text, re.M))
        if not paths and probe.returncode == 0 and 'statically linked' not in text:
            raise ValueError('empty simulator library closure')
        return sorted((Path(p).name, digest(p)) for p in paths)
    if platform.system() == 'Darwin':
        # System dylibs live in the OS shared cache; the exact OS build identifies them.
        result, pending, seen = {}, [binary], set()
        while pending:
            image = Path(pending.pop()).resolve()
            if image in seen:
                continue
            seen.add(image)
            text = subprocess.check_output(['otool', '-L', str(image)], text=True)
            for line in text.splitlines()[1:]:
                name = line.strip().split(' (', 1)[0]
                if name.startswith(('/usr/lib/', '/System/Library/')):
                    continue
                path = Path(name.replace('@loader_path', str(image.parent)).replace('@executable_path', str(binary.parent)))
                if not path.is_absolute() or not path.is_file():
                    raise ValueError(f'unresolved simulator library: {name}')
                if path.resolve() != image:
                    result[name] = digest(path)
                    pending.append(path)
        return result
    raise ValueError('execution reuse is unavailable on this host')


def origin():
    return dict(commit=subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
                run=os.environ.get('GITHUB_RUN_ID'), attempt=os.environ.get('GITHUB_RUN_ATTEMPT'),
                repository=os.environ.get('GITHUB_REPOSITORY'))


def runtime(binary, kind, metadata):
    binary = Path(binary).resolve()
    files = ['sims/program-test/execution.py', f'sims/{"program-test" if kind == "native" else "arch-test"}/run.py',
             'sims/Makefile', 'sims/config.mk', f'sims/{"program-test" if kind == "native" else "arch-test"}/Makefile.inc',
             f'.github/workflows/ci-{"software" if kind == "native" else "arch"}.yml']
    if kind == 'arch':
        files += ['sw/riscv-arch-test/run_tests.py', 'sims/arch-test/shard.py']
    relevant_env = {k: v for k, v in os.environ.items()
                    if k in ('LANG', 'TZ', 'GLIBC_TUNABLES') or k.startswith(
                        ('LC_', 'LD_', 'DYLD_', 'OMP_', 'ASAN_', 'UBSAN_', 'TSAN_', 'RHEG_', 'SPIKE_', 'SAIL_'))}
    if platform.system() == 'Linux':
        fields = {'vendor_id', 'cpu family', 'model', 'stepping', 'flags', 'Features',
                  'CPU implementer', 'CPU architecture', 'CPU variant', 'CPU part', 'CPU revision'}
        cpu = sorted({line.strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
                      if line.partition(':')[0].strip() in fields})
    elif platform.system() == 'Darwin':
        cpu = subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip()
    else:
        cpu = platform.processor()
    identity = dict(binary=digest(binary), libraries=libraries(binary),
                    platform=[platform.system(), platform.machine(), platform.release(), platform.version(), platform.python_version()],
                    cpu=[cpu, os.cpu_count()],
                    image=[os.environ.get('ImageOS'), os.environ.get('ImageVersion')], environment=relevant_env,
                    contract={name: digest(ROOT / name) for name in files},
                    target={k: metadata.get(k) for k in ('soc', 'target_fingerprint', 'configuration_fingerprint', 'backend', 'variant')},
                    kind=kind)
    if metadata and metadata.get('sha256') != identity['binary']:
        raise ValueError('simulator checksum differs from its attestation')
    if 'trace' in metadata.get('variant', '').split('-'):
        raise ValueError('traced runs must execute freshly')
    return dict(namespace=fingerprint(identity), runtime=identity, binary=str(binary), stamp=stamp(binary), origin=origin())


class Evidence:
    """Persist only successful process output; never persist an authoritative verdict."""
    def __init__(self, binary, kind, metadata=None, directory=None, force_fresh=False):
        selected = directory or os.environ.get('RHODIUM_EXECUTION_CACHE')
        self.directory = Path(selected) if selected else None
        self.force_fresh = force_fresh or os.environ.get('RHODIUM_FORCE_FRESH_EXECUTION') == '1'
        self.context = None
        if self.directory is None:
            return
        try:
            context_path = os.environ.get('RHODIUM_EXECUTION_CONTEXT')
            if context_path:
                if not Path(context_path).is_file():
                    return  # Preparation already reported why this job cannot reuse evidence.
                self.context = json.loads(Path(context_path).read_text())
                if (self.context['binary'] != str(Path(binary).resolve()) or self.context['stamp'] != stamp(binary)
                        or self.context['runtime']['kind'] != kind
                        or fingerprint(self.context['runtime']) != self.context['namespace']):
                    raise ValueError('prepared execution context does not match this simulator')
            else:
                sidecar = Path(str(binary) + '.json')
                if metadata is None and sidecar.is_file():
                    metadata = json.loads(sidecar.read_text())
                self.context = runtime(binary, kind, metadata or {})
            self.directory.mkdir(parents=True, exist_ok=True)
        except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
            print(f'execution evidence disabled: {error}', file=sys.stderr)
            self.context = None

    def key(self, contract, files):
        if self.context is None:
            return None
        return fingerprint(dict(namespace=self.context['namespace'], contract=contract,
                                files={name: digest(path) for name, path in sorted(files.items())}))

    def read(self, key, *, inspect=False):
        if key is None or self.force_fresh and not inspect:
            return None
        try:
            record = json.loads((self.directory / (key + '.json')).read_text())
            log = self.directory / (key + '.log')
            if (record['key'] != key or record['namespace'] != self.context['namespace']
                    or record['returncode'] != 0 or record['log_sha256'] != digest(log)):
                raise ValueError('execution evidence checksum or identity mismatch')
            if not isinstance(record['origin'], dict) or not record['origin'].get('commit'):
                raise ValueError('missing execution provenance')
            return record, log
        except FileNotFoundError:
            return None
        except (OSError, ValueError, KeyError, TypeError) as error:
            print(f'ignoring invalid execution evidence: {error}', file=sys.stderr)
            return None

    def info(self, key, cached=None):
        if key is None:
            return dict(reused=False)
        return dict(key=key, namespace=self.context['namespace'], reused=cached is not None,
                    origin=cached[0]['origin'] if cached else self.context['origin'])

    def save(self, key, log, returncode, seconds):
        if key is None or returncode != 0:
            return
        temporary = []
        try:
            record = dict(key=key, namespace=self.context['namespace'], returncode=returncode,
                          seconds=seconds, log_sha256=digest(log), origin=self.context['origin'])
            for suffix in ('.log', '.json'):
                fd, name = tempfile.mkstemp(dir=self.directory, suffix=suffix)
                os.close(fd)
                path = Path(name)
                temporary.append(path)
                if suffix == '.log':
                    shutil.copyfile(log, path)
                else:
                    path.write_text(json.dumps(record, sort_keys=True) + '\n')
                path.replace(self.directory / (key + suffix))
        except OSError as error:
            print(f'could not save execution evidence: {error}', file=sys.stderr)
        finally:
            for path in temporary:
                try:
                    path.unlink(missing_ok=True)
                except OSError:
                    pass


def log_info(text):
    lines = [line[len(MARKER):] for line in text.splitlines() if line.startswith(MARKER)]
    if len(lines) > 1:
        raise ValueError('duplicate execution evidence marker')
    return json.loads(lines[0]) if lines else None


def prepare(args):
    metadata = json.loads(args.metadata.read_text())
    context = runtime(args.simulator, args.kind, metadata)
    inputs, results = [], []
    if args.kind == 'native':
        if not args.manifest:
            raise ValueError('native execution requires manifests')
        for path in args.manifest:
            manifest = json.loads(path.read_text())
            if not manifest['tests'] or manifest.get('build_failures'):
                raise ValueError('execution inputs must be complete')
            tests = sorted(manifest['tests'], key=lambda t: t['name'])[args.shard_index::args.shard_count]
            if not tests or any('litmus_allowed_states' in t or len(t.get('harts', [0])) != 1 for t in tests):
                raise ValueError('execution reuse requires nonempty single-hart non-litmus work')
            files = {t['elf']: t['sha256'] for t in tests}
            files.update({t['payload']['elf']: t['payload']['sha256'] for t in tests if 'payload' in t})
            files.update({f['path']: f['sha256'] for f in manifest.get('files', [])})
            for name, sha in files.items():
                if digest(path.parent / name) != sha:
                    raise ValueError('execution input checksum mismatch')
            inputs.append(dict(suite=manifest['suite'], target=manifest['target'], tests=tests, files=files))
            output = path.parent / 'results'
            if args.shard_count > 1:
                output /= f'shard-{args.shard_index}-of-{args.shard_count}'
            results.append(dict(path=str(output / 'results.json'), names=[t['name'] for t in tests]))
    else:
        if args.elf_dir is None:
            raise ValueError('ACT execution requires an ELF directory')
        elfs = sorted(args.elf_dir.rglob('*.elf'))[args.shard_index::args.shard_count]
        if not elfs:
            raise ValueError('execution reuse requires a nonempty ACT shard')
        inputs = {p.relative_to(args.elf_dir).as_posix(): digest(p) for p in elfs}
        output = args.elf_dir.parent
        if args.shard_count > 1:
            output = output / 'shards' / str(args.shard_index)
        results.append(dict(path=str(output / 'results.json'), names=list(inputs)))
    context.update(results=results, key=fingerprint(dict(namespace=context['namespace'], inputs=inputs, selection=json.loads(args.selection))))
    args.output.write_text(json.dumps(context, sort_keys=True, indent=2) + '\n')
    print('key=' + context['key'])


def ready(context_path, directory):
    """Require the complete current selection and verified evidence for every passing case."""
    context = json.loads(context_path.read_text())
    metadata = dict(context['runtime']['target'], sha256=context['runtime']['binary'])
    cache = Evidence(context['binary'], context['runtime']['kind'], metadata, directory=directory)
    if cache.context is None or cache.context['namespace'] != context['namespace']:
        return False
    for expected in context['results']:
        result = json.loads(Path(expected['path']).read_text())
        tests = result['tests']
        if sorted(t['name'] for t in tests) != sorted(expected['names']):
            return False
        if ((context['runtime']['kind'] == 'native' and result.get('complete') is not True)
                or result.get('pending') or result.get('build_failures')
                or not tests or any(t.get('functional_status', t['status']) != 'passed' for t in tests)):
            return False
        for test in tests:
            info = test.get('execution', {})
            key = info.get('key')
            if not isinstance(key, str) or not re.fullmatch('[0-9a-f]{64}', key) or not cache.read(key, inspect=True):
                return False
    return bool(context['results'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='mode', required=True)
    p = commands.add_parser('prepare')
    p.add_argument('--kind', choices=('native', 'arch'), required=True)
    p.add_argument('--simulator', type=Path, required=True)
    p.add_argument('--metadata', type=Path, required=True)
    p.add_argument('--manifest', type=Path, action='append', default=[])
    p.add_argument('--elf-dir', type=Path)
    p.add_argument('--shard-index', type=int, default=0)
    p.add_argument('--shard-count', type=int, default=1)
    p.add_argument('--selection', required=True, help='execution options, excluding commit and expectations')
    p.add_argument('--output', type=Path, required=True)
    p = commands.add_parser('ready')
    p.add_argument('--context', type=Path, required=True)
    p.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.mode == 'prepare':
            if args.shard_count <= 0 or not 0 <= args.shard_index < args.shard_count:
                parser.error('invalid shard')
            prepare(args)
        else:
            print('ready=' + str(ready(args.context, args.directory)).lower())
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        # Cache availability never changes the test verdict; callers execute on a miss.
        print(f'execution evidence unavailable: {error}', file=sys.stderr)
        print('key=' if args.mode == 'prepare' else 'ready=false')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
