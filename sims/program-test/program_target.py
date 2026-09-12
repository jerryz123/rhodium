#!/usr/bin/env python3
# Validates program-target descriptors, fingerprints them, and inspects RISC-V ELF attributes.
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def validate_target(target):
    required = {'soc', 'xlen', 'extensions', 'march', 'mabi', 'ram'}
    if not isinstance(target, dict) or not required <= set(target):
        raise ValueError(f'program target must contain {sorted(required)}')
    extensions = target['extensions']
    if (not isinstance(target['soc'], str) or not target['soc'] or target['xlen'] not in (32, 64)
            or not isinstance(extensions, list) or not extensions
            or any(not isinstance(extension, str) or not extension for extension in extensions)
            or len(extensions) != len(set(extensions)) or 'i' not in extensions
            or not isinstance(target['march'], str) or not target['march'].startswith(f'rv{target["xlen"]}i')
            or not isinstance(target['mabi'], str) or not target['mabi']
            or not isinstance(target['ram'], list) or not target['ram']
            or any(not isinstance(region, dict) or set(region) != {'base', 'size'}
                   or not isinstance(region['base'], int) or not isinstance(region['size'], int)
                   or region['base'] < 0 or region['size'] <= 0 for region in target['ram'])):
        raise ValueError('invalid program target descriptor')
    return target


def load_target(path):
    return validate_target(json.loads(Path(path).read_text()))


def target_fingerprint(target):
    encoded = json.dumps(target, sort_keys=True, separators=(',', ':')).encode()
    return hashlib.sha256(encoded).hexdigest()


def readelf_for(compiler):
    candidate = str(compiler).removesuffix('gcc') + 'readelf'
    readelf = shutil.which(candidate)
    if not readelf:
        raise RuntimeError(f'RISC-V readelf not found next to compiler: {candidate}')
    return readelf


def objdump_for(compiler):
    candidate = str(compiler).removesuffix('gcc') + 'objdump'
    objdump = shutil.which(candidate)
    if not objdump:
        raise RuntimeError(f'RISC-V objdump not found next to compiler: {candidate}')
    return objdump


def elf_architecture(readelf, elf):
    attributes = subprocess.check_output([readelf, '-A', str(elf)], text=True)
    match = re.search(r'Tag_RISCV_arch:\s*"([^"]+)"', attributes)
    if not match:
        raise RuntimeError(f'{elf}: missing Tag_RISCV_arch')
    return match.group(1)


def instruction_inventory(objdump, elf):
    disassembly = subprocess.check_output([objdump, '-d', '-M', 'no-aliases', str(elf)], text=True)
    mnemonics = {}
    compressed = 0
    unknown = 0
    for line in disassembly.splitlines():
        match = re.match(r'^\s*[0-9a-f]+:\s+([0-9a-f]{4}|[0-9a-f]{8})\s+(\S+)', line)
        if not match:
            continue
        encoding, mnemonic = match.groups()
        mnemonics[mnemonic] = mnemonics.get(mnemonic, 0) + 1
        compressed += len(encoding) == 4
        unknown += mnemonic.startswith('.') or 'unimp' in mnemonic
    count = sum(mnemonics.values())
    if not count:
        raise RuntimeError(f'{elf}: objdump found no executable instructions')
    return dict(instruction_count=count, compressed_instruction_count=compressed,
                unknown_instruction_count=unknown,
                mnemonics=dict(sorted(mnemonics.items())))


def probe_compiler(compiler, march, mabi, build):
    probe = Path(build) / 'compiler-target-probe.o'
    result = subprocess.run([str(compiler), f'-march={march}', f'-mabi={mabi}',
                             '-c', '-x', 'c', '/dev/null', '-o', str(probe)],
                            capture_output=True, text=True)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or 'compiler rejected target'
        raise RuntimeError(f'GNU RISC-V target {march}/{mabi} is unavailable: {detail}')
    try:
        return elf_architecture(readelf_for(compiler), probe)
    finally:
        probe.unlink(missing_ok=True)
