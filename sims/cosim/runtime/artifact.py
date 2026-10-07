#!/usr/bin/env python3
# Records and verifies exact-source native cosim libraries before simulator linking.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[3]
FILES = ('link-flags.txt', 'libsail_checker.a', 'libsail_reference.a',
         'librv5stage_cosim.a', 'librv2wide_cosim.a', 'libcosim_observation.a')


def environment(directory):
    """Bind ABI, checkout, source content, and absolute link paths to the producer."""
    source = ROOT / 'sims/cosim'
    inputs = [source / 'CMakeLists.txt']
    inputs += sorted(path for owner in ('events', 'sail', 'rv5stage', 'rv2wide')
                     for path in (source / owner).iterdir() if path.suffix in ('.cc', '.h'))
    digest = hashlib.sha256()
    for path in inputs:
        digest.update(str(path.relative_to(ROOT)).encode() + b'\0' + path.read_bytes())
    return dict(schema=1, commit=subprocess.check_output(
        ['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        system=platform.system(), machine=platform.machine(), workspace=str(ROOT),
        directory=str(directory.resolve()), sources=digest.hexdigest())


def file_hashes(directory):
    """Require the complete archive inventory and all referenced Sail libraries."""
    files = {name: hashlib.sha256((directory / name).read_bytes()).hexdigest() for name in FILES}
    archives = [Path(flag) for flag in shlex.split((directory / 'link-flags.txt').read_text())
                if flag.endswith('.a')]
    if not archives or any(not archive.is_file() for archive in archives):
        raise ValueError('cosim link flags reference missing native libraries')
    if not {directory.resolve() / name for name in FILES[1:]} <= set(archives):
        raise ValueError('cosim link flags omit runtime libraries')
    return files


def record(directory):
    """Publish a checksum manifest after the runtime-library build completes."""
    manifest = environment(directory) | dict(files=file_hashes(directory))
    (directory / 'artifact.json').write_text(json.dumps(manifest, indent=2) + '\n')


def verify(directory):
    """Reject stale, relocated, foreign-platform, incomplete, or corrupt libraries."""
    manifest = json.loads((directory / 'artifact.json').read_text())
    expected = environment(directory) | dict(files=file_hashes(directory))
    if manifest != expected:
        raise ValueError('cosim runtime artifact does not match this checkout or its native libraries')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('record', 'verify'))
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        (record if args.mode == 'record' else verify)(args.directory)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
