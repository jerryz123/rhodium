#!/usr/bin/env python3
# Validates shared suite ELFs and binds an execution manifest to a concrete SoC target.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build import check_elf_memory
from program_target import elf_build_spec, load_target, target_fingerprint, validate_target


def bind(manifest_path, target, output):
    root = manifest_path.parent.resolve()
    if output.resolve() == manifest_path.resolve() or output.parent.resolve() != root:
        raise ValueError('write the execution manifest beside, separately from, the build manifest')
    manifest = json.loads(manifest_path.read_text())
    source_target = validate_target(manifest['target'])
    validate_target(target)
    if manifest.get('target_fingerprint') != target_fingerprint(source_target):
        raise ValueError('invalid build target fingerprint')
    spec = manifest['build_spec']
    if (manifest.get('build_spec_fingerprint') != target_fingerprint(spec)
            or spec != elf_build_spec(source_target, manifest['suite'], spec['options'])):
        raise ValueError('invalid ELF build specification')
    if spec != elf_build_spec(target, manifest['suite'], spec['options']):
        raise ValueError('shared ELF build specification does not match the execution target')
    tests = manifest['tests']
    if not tests or manifest.get('build_failures'):
        raise ValueError('shared ELF suite must be complete and nonempty')
    names = [test['name'] for test in tests]
    if len(names) != len(set(names)):
        raise ValueError('shared ELF test names must be unique')
    for test in tests:
        relative = PurePosixPath(test['elf'])
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('invalid shared ELF path')
        elf = (root / test['elf']).resolve()
        if not elf.is_relative_to(root) or not elf.is_file():
            raise ValueError('shared ELF is missing or outside its suite')
        data = elf.read_bytes()
        if hashlib.sha256(data).hexdigest() != test['sha256']:
            raise ValueError('shared ELF checksum mismatch')
        if data[:5] != b'\x7fELF' + bytes([2 if target['xlen'] == 64 else 1]):
            raise ValueError('shared ELF class does not match target XLEN')
        test['load_segments'] = check_elf_memory(elf, target['ram'], require_executable_entry=True)
        harts = test.get('harts', [0])
        if not harts or any(hart not in target['harts'] for hart in harts):
            raise ValueError('shared ELF boot harts do not match the execution target')
    manifest.update(build_target=source_target, target=target,
                    target_fingerprint=target_fingerprint(target))
    output.write_text(json.dumps(manifest, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        bind(args.manifest, load_target(args.target), args.output)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
