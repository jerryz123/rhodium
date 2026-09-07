#!/usr/bin/env python3
# Partitions the complete generated ACT inventory without changing architectural selection.
import argparse
import json
from pathlib import Path


def partition(elf_dir, output, index, count):
    if count < 1 or not 0 <= index < count:
        raise ValueError('shard index must be within a positive shard count')
    elfs = sorted(elf_dir.resolve().rglob('*.elf'))
    if not elfs:
        raise ValueError('cannot partition an empty ACT inventory')
    selected = elfs[index::count]
    if not selected:
        raise ValueError('empty ACT shard; reduce the shard count')
    output = output.resolve()
    if output == elf_dir.resolve() or elf_dir.resolve() in output.parents or output in elf_dir.resolve().parents:
        raise ValueError('shard output and generated ELF directory must be separate trees')
    # Delete only previously generated shard links, never source ELFs or other files.
    existing = list(output.rglob('*.elf')) if output.exists() else []
    inventory = output.parent / 'inventory.json'
    if existing:
        previous = json.loads(inventory.read_text()) if inventory.exists() else {}
        if previous.get('kind') != 'rhodium-act-shard-v1':
            raise ValueError('existing ELF directory is not an owned ACT shard')
        if any(str(path.relative_to(output)) not in previous['tests'] for path in existing):
            raise ValueError('shard directory contains unowned ELF paths')
    for path in existing:
        if not path.is_symlink():
            raise ValueError(f'refusing to replace a non-shard ELF: {path}')
    for path in existing:
        path.unlink()
    for elf in selected:
        destination = output / elf.relative_to(elf_dir.resolve())
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.symlink_to(elf)
    inventory.write_text(json.dumps(dict(
        kind='rhodium-act-shard-v1', index=index, count=count, total=len(elfs),
        tests=[str(elf.relative_to(elf_dir.resolve())) for elf in selected]), indent=2) + '\n')
    return selected


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--index', type=int, required=True)
    parser.add_argument('--count', type=int, required=True)
    args = parser.parse_args()
    print(f'Selected {len(partition(args.elf_dir, args.output, args.index, args.count))} ACT ELFs')
