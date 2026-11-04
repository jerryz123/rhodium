# Resolves shared ELF producers from generated target descriptors and validates their archived build contracts.
# SPDX-License-Identifier: Apache-2.0

import argparse
import errno
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import tarfile
import tempfile

from .policy import ROOT, workload_requests
from .programs import NATIVE_COMPILER, elf_build_spec, load_target, target_fingerprint, group_builds, harness_entries
from sw.build.program_target import native_toolchain
from .inputs import SOURCES
from .catalog import ARCHIVES, materialize
from sw.build.litmus_smoke import validate_smoke_manifest


def local_catalog(socs):
    """Bind existing SoC descriptors using the same enrolled products and resolver as CI."""
    sources = {soc: ROOT / '.rhodium-cache/program-targets' / (soc + '.json') for soc in socs}
    for soc, path in sources.items():
        if not path.is_file():
            raise ValueError(f'missing target descriptor; run moon run rhodium:target--{soc} first')
        if load_target(path)['soc'] != soc:
            raise ValueError('descriptor does not match requested SoC: ' + soc)
    plan = workload_requests(lambda kind, soc, *parts: soc in socs, local=True)
    cache = ROOT / '.rhodium-cache/moon/local-targets'
    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=cache) as tmp:
        directory = Path(tmp) / 'targets'
        directory.mkdir()
        for soc in sorted(socs):
            shutil.copyfile(sources[soc], directory / (soc + '.json'))
        resolve(plan, directory, local=True)
        # A failed refresh never replaces the directory referenced by the active catalog.
        identity = hashlib.sha256()
        for path in sorted(directory.iterdir()):
            identity.update(path.name.encode() + b'\0' + path.read_bytes() + b'\0')
        snapshot = cache / identity.hexdigest()
        try:
            directory.rename(snapshot)
        except OSError as error:
            if error.errno not in (errno.EEXIST, errno.ENOTEMPTY) or not snapshot.is_dir():
                raise
    materialize(snapshot, local=True)
    print(f'Prepared local ELF bindings for {", ".join(sorted(socs))}')


def resolve(plan, directory, *, local=False):
    """Resolve compatible artifacts, never GitHub jobs or execution shards."""
    if not local:
        selected = set(plan['tasks'])
        plan = workload_requests(lambda *parts: ':'.join(parts) in selected)
    targets = {soc: load_target(directory / (soc + '.json')) for soc in plan['targets']}
    requests, builds, bindings = {}, {}, {}
    for kind, entries in [('native', plan['programs']), ('harness', harness_entries(plan['simulators'])),
                          ('platform', plan['platforms']), ('linux', plan['linux'])]:
        groups = group_builds(entries, targets)
        for build in groups['build']:
            key = build['build_id']
            if key in requests:
                raise ValueError('two recipes claim one ELF identity: ' + key)
            requests[key] = dict(kind=kind, suite=build['suite'], options=build['options'],
                                 targets=sorted(build['targets']))
            # A fixed slot exists for every enrolled SoC/suite. Only the chosen
            # representative builds a compatible archive; other slots skip.
            slot = f"{kind}:{build['soc']}:{build['suite']}"
            builds[slot] = build
        for row in groups['run']:
            bindings[f"{kind}:{row['soc']}:{row['suite']}"] = row['build_id']
        if kind == 'harness':
            (directory / 'harness-plan.json').write_text(json.dumps(groups) + '\n')
    (directory / 'elf-requests.json').write_text(json.dumps(requests, sort_keys=True) + '\n')
    (directory / 'elf-bindings.json').write_text(json.dumps(bindings, sort_keys=True) + '\n')
    return dict(builds=builds, bindings=bindings)


def validate_manifest(manifest, key, request):
    if request['suite'] == 'litmus':
        validate_smoke_manifest(manifest, request['options'])
    spec = manifest['build_spec']
    target = manifest['target']
    if (manifest['suite'] != request['suite'] or spec != elf_build_spec(target, manifest['suite'], spec['options'])
            or any(k not in spec['options'] or spec['options'][k] != v for k, v in request['options'].items())
            or manifest['build_spec_fingerprint'] != target_fingerprint(spec)
            or key != group(target, request)):
        raise ValueError('archive does not match its requested build specification: ' + key)


def group(target, request):
    suite = request['suite']
    return suite + '-' + target_fingerprint(elf_build_spec(target, suite, request['options']))


def descriptor(directory, soc, key, request):
    path = directory / (soc + '.json')
    target = load_target(path)
    if soc not in request['targets'] or target['soc'] != soc or group(target, request) != key:
        raise ValueError('descriptor does not match ELF group: ' + soc)
    return path


def producer_descriptor(directory, key, request):
    soc = next((s for s in request['targets'] if s == os.environ.get('SOC')), request['targets'][0])
    return soc, descriptor(directory, soc, key, request)


def pack_archive(key, request, manifest):
    """Validate and package an artifact already compiled by its Moon recipe."""
    validate_manifest(json.loads(manifest.read_text()), key, request)
    subprocess.run(['python3', 'sims/program-test/archive.py', '--manifest', manifest,
                    '--archive', ARCHIVES / (key + '.tar.gz')], check=True)


def check_source(request):
    """Reject a dirty or unpinned workload source before Moon restores or builds it."""
    suite = request['suite']
    submodule = 'sw/' + SOURCES[suite]
    revision = subprocess.check_output(['git','rev-parse','HEAD:' + submodule])
    if subprocess.check_output(['git','-C',submodule,'rev-parse','HEAD']) != revision:
        raise ValueError('unexpected submodule revision: ' + submodule)
    subprocess.run(['git','-C',submodule,'diff','--quiet','HEAD','--ignore-submodules=untracked'],check=True)


def bind_archive(directory, key, request, soc, archive, output):
    path = descriptor(directory, soc, key, request)
    with tarfile.open(archive, 'r:gz') as stream:
        validate_manifest(json.load(stream.extractfile('manifest.json')), key, request)
        if any(not entry.isfile() or Path(entry.name).is_absolute() or '..' in Path(entry.name).parts
               for entry in stream.getmembers()):
            raise ValueError('unsafe workload archive: ' + str(archive))
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['tar','-xzf',archive,'-C',output.parent], check=True)
    subprocess.run(['python3','sw/build/bind.py','--manifest',output.parent / 'manifest.json',
                    '--target',path,'--output',output], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('op', choices=['resolve','check-source','prepare','pack','fingerprint','verify','bind'])
    parser.add_argument('key', nargs='?')
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--soc')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--targets', type=Path, default=os.environ.get('PROGRAM_TARGETS'))
    args = parser.parse_args()
    if args.targets is None:
        parser.error('target descriptors are required (--targets or PROGRAM_TARGETS)')
    directory = args.targets.resolve()
    if args.op == 'resolve':
        artifacts = resolve(json.loads(os.environ['SELECTION']), directory)
        if 'GITHUB_OUTPUT' in os.environ:
            with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
                for key, value in artifacts.items():
                    print(f'{key}={json.dumps(value, separators=(",", ":"))}', file=stream)
        return
    request = json.loads((directory / 'elf-requests.json').read_text())[args.key]
    if args.op == 'check-source':
        check_source(request)
    elif args.op == 'prepare':
        descriptor(directory, args.soc, args.key, request)
    elif args.op == 'pack':
        if args.manifest is None:
            parser.error('pack requires --manifest')
        pack_archive(args.key, request, args.manifest)
    elif args.op == 'fingerprint':
        _, path = producer_descriptor(directory, args.key, request)
        print(target_fingerprint(native_toolchain(request['suite'], NATIVE_COMPILER, load_target(path))))
    elif args.op == 'bind':
        bind_archive(directory, args.key, request, args.soc, args.archive, args.output)
    else:
        with tarfile.open(args.archive, 'r:gz') as stream:
            validate_manifest(json.load(stream.extractfile('manifest.json')),args.key,request)


if __name__ == '__main__':
    main()
