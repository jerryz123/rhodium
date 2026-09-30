#!/usr/bin/env python3
# Packages complete ACT ELF inventories and verifies cached payloads before reuse.
# SPDX-License-Identifier: Apache-2.0
"""The caller must finish the full ACT build successfully before packaging."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import tarfile


PAYLOAD_FILES = ("elfs.tar.gz", "configuration.json", "inventory.json")


def digest(stream):
    """Hash file contents without loading the complete ELF archive into memory."""
    result = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        result.update(block)
    return result.hexdigest()


def file_digest(path):
    """Hash a payload file or dereferenced upstream ELF."""
    with path.open("rb") as stream:
        return digest(stream)


def checksum_text(bundle):
    """Cover the archive, configuration, and inventory with the published checksums."""
    return "".join(f"{file_digest(bundle / name)}  {name}\n" for name in PAYLOAD_FILES)


def valid_elf_name(name):
    """Accept only canonical relative ELF paths safe for archive extraction."""
    path = PurePosixPath(name)
    return (not path.is_absolute() and ".." not in path.parts
            and path.as_posix() == name and path.suffix == ".elf")


def package(elf_dir, configuration, bundle, identity):
    """Snapshot every generated ELF, dereferencing ACT's links into standalone files."""
    elfs = sorted(elf_dir.rglob("*.elf"))
    if not elfs:
        raise ValueError("cannot package an empty ACT ELF inventory")
    inventory = {}
    for elf in elfs:
        name = elf.relative_to(elf_dir).as_posix()
        if not valid_elf_name(name) or not elf.is_file():
            raise ValueError(f"invalid ACT ELF: {elf}")
        inventory[name] = file_digest(elf)
    bundle.mkdir(parents=True, exist_ok=True)
    with tarfile.open(bundle / "elfs.tar.gz", "w:gz", dereference=True) as archive:
        for elf in elfs:
            archive.add(elf, arcname=elf.relative_to(elf_dir).as_posix(), recursive=False)
    shutil.copyfile(configuration, bundle / "configuration.json")
    (bundle / "inventory.json").write_text(json.dumps({
        "schema": 1, "identity": identity, "elfs": inventory,
    }, sort_keys=True, indent=2) + "\n")
    (bundle / "elfs.sha256").write_text(checksum_text(bundle))
    verify(bundle, configuration, identity)


def verify(bundle, configuration, identity=None):
    """Reject mismatched inputs, corrupt contents, and incomplete or unsafe inventories."""
    if (bundle / "elfs.sha256").read_text() != checksum_text(bundle):
        raise ValueError("ACT payload checksum mismatch")
    if (bundle / "configuration.json").read_bytes() != configuration.read_bytes():
        raise ValueError("ACT payload configuration mismatch")
    manifest = json.loads((bundle / "inventory.json").read_text())
    if manifest.get("schema") != 1 or not isinstance(manifest.get("identity"), str):
        raise ValueError("invalid ACT payload inventory schema")
    if identity is not None and manifest["identity"] != identity:
        raise ValueError("ACT payload input identity mismatch")
    expected = manifest.get("elfs")
    if not isinstance(expected, dict) or not expected or not all(valid_elf_name(name) for name in expected):
        raise ValueError("invalid or empty ACT ELF inventory")
    actual = {}
    with tarfile.open(bundle / "elfs.tar.gz", "r:gz") as archive:
        for member in archive:
            if not member.isfile() or not valid_elf_name(member.name) or member.name in actual:
                raise ValueError(f"invalid ACT archive member: {member.name}")
            with archive.extractfile(member) as stream:
                actual[member.name] = digest(stream)
    if actual != expected:
        raise ValueError("ACT archive does not match its complete ELF inventory")
    print(f"Verified {len(actual)} ACT ELFs")


def main():
    """Provide package and verify commands for CI producers, cache hits, and consumers."""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("package", "verify"):
        child = commands.add_parser(command)
        child.add_argument("--bundle", type=Path, required=True)
        child.add_argument("--configuration", type=Path, required=True)
        child.add_argument("--identity", required=command == "package")
        if command == "package":
            child.add_argument("--elf-dir", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "package":
        package(args.elf_dir, args.configuration, args.bundle, args.identity)
    else:
        verify(args.bundle, args.configuration, args.identity)


if __name__ == "__main__":
    main()
