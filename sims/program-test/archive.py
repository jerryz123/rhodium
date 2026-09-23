#!/usr/bin/env python3
# Packages manifest-selected RISC-V ELFs for exact CI workload replay.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile


def package(manifest_path, archive_path):
    root = manifest_path.parent.resolve()
    manifest = json.loads(manifest_path.read_text())
    tests = manifest['tests']
    if not tests:
        raise ValueError('workload manifest contains no ELFs')
    binaries = {}
    for test in tests:
        name = test['elf']
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or name == 'manifest.json':
            raise ValueError(f'invalid manifest ELF path: {name}')
        binary = (root / name).resolve()
        if not binary.is_relative_to(root) or not binary.is_file():
            raise ValueError(f'manifest ELF is missing or outside its root: {name}')
        if hashlib.sha256(binary.read_bytes()).hexdigest() != test['sha256']:
            raise ValueError(f'manifest ELF checksum mismatch: {name}')
        binaries[name] = binary
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive_path, 'w:gz') as archive:
        archive.add(manifest_path, arcname='manifest.json')
        for name, binary in sorted(binaries.items()):
            archive.add(binary, arcname=name)
    print(f'Packaged {len(binaries)} ELFs from {manifest_path} into {archive_path}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    args = parser.parse_args()
    package(args.manifest, args.archive)


if __name__ == '__main__':
    main()
