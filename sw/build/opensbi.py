#!/usr/bin/env python3
# Builds and validates an OpenSBI FW_JUMP image from a concrete SoC target description.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from program_target import load_target, readelf_for  # noqa: E402


FDT_RESERVATION_SIZE = 64 * 1024
NEXT_STAGE_ALIGNMENT = 2 * 1024 * 1024
REQUIRED_EXTENSIONS = {'i', 'm', 'a', 'zicsr', 'zifencei', 'zicntr'}


def align_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def align_down(value, alignment):
    return value // alignment * alignment


def containing_region(regions, address):
    return next((region for region in regions
                 if region['base'] <= address < region['base'] + region['size']), None)


def target_layout(target):
    missing = REQUIRED_EXTENSIONS - set(target['extensions'])
    if target['xlen'] != 64 or missing:
        detail = f'; missing {sorted(missing)}' if missing else ''
        raise ValueError(f'OpenSBI qualification requires an RV64 IMA_Zicsr_Zifencei_Zicntr target{detail}')
    if target.get('harts') != [0]:
        raise ValueError('OpenSBI qualification currently requires exactly bootable hart 0')
    boot = target.get('boot')
    if not isinstance(boot, dict) or 'payload_address' not in boot:
        raise ValueError('OpenSBI requires boot.payload_address in the target descriptor')
    firmware = boot['payload_address']
    region = containing_region(target['ram'], firmware)
    if region is None:
        raise ValueError('OpenSBI firmware address is outside target RAM')
    region_end = region['base'] + region['size']
    next_stage = align_up(firmware + 1, NEXT_STAGE_ALIGNMENT)
    fdt = align_down(region_end - FDT_RESERVATION_SIZE, 4096)
    if next_stage <= firmware or next_stage >= fdt:
        raise ValueError('target RAM cannot hold OpenSBI, its next stage, and the relocated FDT')
    extensions = set(target['extensions'])
    isa = 'rv64ima' + ('c' if 'c' in extensions else '') + '_zicsr_zifencei'
    return {
        'soc': target['soc'],
        'xlen': target['xlen'],
        'firmware_address': firmware,
        'firmware_link_address': 0,
        'next_stage_address': next_stage,
        'fdt_address': fdt,
        'fdt_reservation_size': FDT_RESERVATION_SIZE,
        'ram_base': region['base'],
        'ram_size': region['size'],
        'opensbi_isa': isa,
        'opensbi_abi': 'lp64',
    }


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, sort_keys=True, indent=2) + '\n')


def load_layout(path):
    layout = json.loads(Path(path).read_text())
    required = {'soc', 'xlen', 'firmware_address', 'firmware_link_address',
                'next_stage_address', 'fdt_address',
                'fdt_reservation_size', 'ram_base', 'ram_size',
                'opensbi_isa', 'opensbi_abi'}
    if not isinstance(layout, dict) or set(layout) != required:
        raise ValueError('invalid OpenSBI layout descriptor')
    return layout


def validate_fdt(layout, path):
    data = Path(path).read_bytes()
    if len(data) < 8 or data[:4] != b'\xd0\x0d\xfe\xed':
        raise ValueError('OpenSBI FDT is not a flattened device tree')
    total_size = struct.unpack_from('>I', data, 4)[0]
    if total_size < 8 or total_size > len(data):
        raise ValueError('OpenSBI FDT has an invalid total size')
    if total_size > layout['fdt_reservation_size']:
        raise ValueError('OpenSBI FDT exceeds its reserved RAM range')
    return data


def compiler_prefix(compiler):
    compiler = shutil.which(str(compiler))
    if compiler is None:
        raise ValueError('OpenSBI compiler was not found on PATH')
    if not compiler.endswith('gcc'):
        raise ValueError('OpenSBI compiler must end in gcc')
    return compiler[:-3]


def elf_entry(readelf, elf):
    header = subprocess.check_output([readelf, '-h', str(elf)], text=True)
    match = re.search(r'Entry point address:\s*(0x[0-9a-fA-F]+)', header)
    if not match:
        raise RuntimeError(f'{elf}: missing ELF entry point')
    return int(match.group(1), 16)


def elf_load_segments(elf, expected_kind):
    data = Path(elf).read_bytes()
    if len(data) < 64 or data[:7] != b'\x7fELF\x02\x01\x01':
        raise ValueError(f'{elf}: expected a little-endian ELF64 image')
    header = struct.unpack_from('<HHIQQQIHHHHHH', data, 16)
    kind, machine, _, entry, phoff, _, _, _, phsize, phnum, *_ = header
    if kind != expected_kind or machine != 243 or phsize != 56 or phoff + phsize * phnum > len(data):
        raise ValueError(f'{elf}: invalid RISC-V ELF headers')
    segments = []
    for index in range(phnum):
        ptype, flags, offset, virtual, physical, filesz, memsz, _ = struct.unpack_from(
            '<IIQQQQQQ', data, phoff + index * phsize)
        if ptype != 1 or not memsz:
            continue
        if filesz > memsz or offset + filesz > len(data):
            raise ValueError(f'{elf}: invalid load segment')
        segments.append({'virtual': virtual, 'physical': physical, 'size': memsz, 'flags': flags})
    if not segments:
        raise ValueError(f'{elf}: contains no load segments')
    return entry, segments


def require_range(layout, start, size, label):
    ram_end = layout['ram_base'] + layout['ram_size']
    if start < layout['ram_base'] or size <= 0 or start + size > ram_end:
        raise ValueError(f'{label} range 0x{start:x}..0x{start + size:x} exceeds target RAM')


def validate_firmware(layout, firmware):
    entry, segments = elf_load_segments(firmware, 3)
    actual_entry = layout['firmware_address'] + entry
    if actual_entry != layout['firmware_address']:
        raise ValueError('relocated OpenSBI entry does not match the target boot address')
    for segment in segments:
        actual = layout['firmware_address'] + segment['physical']
        require_range(layout, actual, segment['size'], 'OpenSBI')
        if actual + segment['size'] > layout['next_stage_address']:
            raise ValueError('OpenSBI load segment overlaps its next stage')


def validate_smoke(layout, smoke, readelf):
    entry, segments = elf_load_segments(smoke, 2)
    if entry != layout['next_stage_address']:
        raise ValueError('smoke entry does not match the OpenSBI jump address')
    for segment in segments:
        require_range(layout, segment['physical'], segment['size'], 'smoke')
        if segment['physical'] < layout['next_stage_address']:
            raise ValueError('smoke load segment overlaps OpenSBI')
        if segment['physical'] + segment['size'] > layout['fdt_address']:
            raise ValueError('smoke load segment overlaps the relocated FDT reservation')
    symbols = subprocess.check_output([readelf, '-sW', str(smoke)], text=True)
    for symbol in ('tohost', 'fromhost'):
        if re.search(rf'\b{symbol}$', symbols, re.MULTILINE):
            raise ValueError(f'OpenSBI smoke payload must not own {symbol}')


def llvm_tool_directory(requested, output):
    source = Path(requested).resolve()
    tools = {
        name: source / name for name in ('clang', 'llvm-ar', 'llvm-objcopy', 'ld.lld')
    }
    if not tools['ld.lld'].is_file():
        homebrew_lld = source.parents[1] / 'lld/bin/ld.lld'
        located = shutil.which('ld.lld')
        tools['ld.lld'] = homebrew_lld if homebrew_lld.is_file() else Path(located or '')
    missing = [name for name, path in tools.items() if not path.is_file()]
    if missing:
        raise ValueError(f'complete LLVM toolchain is missing {missing}')
    shim = output / 'llvm-tools'
    shim.mkdir(parents=True, exist_ok=True)
    for name, path in tools.items():
        link = shim / name
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(path)
    return shim


def build_firmware(args):
    layout = load_layout(args.layout)
    fdt = Path(args.fdt).resolve()
    fdt_data = validate_fdt(layout, fdt)
    source = Path(args.source).resolve()
    output = Path(args.output).resolve()
    build = output / 'build'
    output.mkdir(parents=True, exist_ok=True)
    configuration = {
        'layout': layout,
        'firmware_options': 1,
        'fdt': str(fdt),
        'fdt_sha256': hashlib.sha256(fdt_data).hexdigest(),
        'source': str(source),
        'compiler': str(args.compiler),
        'llvm': str(Path(args.llvm).resolve()) if args.llvm else None,
        'source_revision': subprocess.check_output(
            ['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip(),
    }
    stamp = output / 'build-configuration.json'
    if build.exists() and (not stamp.exists() or json.loads(stamp.read_text()) != configuration):
        shutil.rmtree(build, ignore_errors=True)
    build.mkdir(parents=True, exist_ok=True)
    write_json(stamp, configuration)
    command = [
        'make', '-C', str(source), f'O={build}', 'PLATFORM=generic',
        'READLINK=readlink',
        f'PLATFORM_RISCV_XLEN={layout["xlen"]}',
        f'PLATFORM_RISCV_ISA={layout["opensbi_isa"]}',
        f'PLATFORM_RISCV_ABI={layout["opensbi_abi"]}',
        f'FW_TEXT_START=0x{layout["firmware_link_address"]:x}',
        f'FW_JUMP_ADDR=0x{layout["next_stage_address"]:x}',
        f'FW_JUMP_FDT_ADDR=0x{layout["fdt_address"]:x}',
        f'FW_FDT_PATH={fdt}',
        'FW_OPTIONS=0x1',
    ]
    if args.llvm:
        llvm = str(llvm_tool_directory(args.llvm, output)) + '/'
        command.append(f'LLVM={llvm}')
        command.append('platform-cflags-y=-Wno-sometimes-uninitialized -Wno-unused-but-set-variable')
    else:
        command.append(f'CROSS_COMPILE={compiler_prefix(args.compiler)}')
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    (output / 'build.log').write_text(result.stdout)
    if result.returncode:
        sys.stderr.write(result.stdout)
        raise RuntimeError(f'OpenSBI build failed with status {result.returncode}')
    firmware_dir = build / 'platform/generic/firmware'
    for name in ('fw_jump.elf', 'fw_jump.bin'):
        shutil.copy2(firmware_dir / name, output / name)
    readelf = readelf_for(args.compiler)
    entry = elf_entry(readelf, output / 'fw_jump.elf')
    if entry != layout['firmware_link_address']:
        raise RuntimeError(f'OpenSBI entry 0x{entry:x} does not match its relocatable link address')
    validate_firmware(layout, output / 'fw_jump.elf')


def build_smoke(args):
    layout = load_layout(args.layout)
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(args.compiler), '-march=rv64ima_zicsr_zicntr', '-mabi=lp64', '-nostdlib', '-nostartfiles',
        '-Wl,--build-id=none', f'-Wl,--defsym,smoke_entry=0x{layout["next_stage_address"]:x}',
        f'-Wl,--defsym,expected_fdt=0x{layout["fdt_address"]:x}',
        f'-Wl,-T,{Path(args.linker).resolve()}', str(Path(args.source).resolve()), '-o', str(output),
    ]
    subprocess.run(command, check=True)
    readelf = readelf_for(args.compiler)
    entry = elf_entry(readelf, output)
    if entry != layout['next_stage_address']:
        raise RuntimeError(f'smoke entry 0x{entry:x} does not match OpenSBI jump address')
    validate_smoke(layout, output, readelf)


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest='command', required=True)

    layout = subparsers.add_parser('layout')
    layout.add_argument('--target', required=True)
    layout.add_argument('--output', required=True)

    firmware = subparsers.add_parser('firmware')
    firmware.add_argument('--layout', required=True)
    firmware.add_argument('--fdt', required=True)
    firmware.add_argument('--source', required=True)
    firmware.add_argument('--compiler', required=True)
    firmware.add_argument('--llvm')
    firmware.add_argument('--output', required=True)

    smoke = subparsers.add_parser('smoke')
    smoke.add_argument('--layout', required=True)
    smoke.add_argument('--source', required=True)
    smoke.add_argument('--linker', required=True)
    smoke.add_argument('--compiler', required=True)
    smoke.add_argument('--output', required=True)

    args = parser.parse_args()
    if args.command == 'layout':
        write_json(args.output, target_layout(load_target(args.target)))
    elif args.command == 'firmware':
        build_firmware(args)
    else:
        build_smoke(args)


if __name__ == '__main__':
    main()
