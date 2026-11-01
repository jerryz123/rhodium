#!/usr/bin/env python3
# Builds a target-bound Buildroot Linux smoke image and packages its existing OpenSBI handoff.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build import check_elf_memory
from opensbi import (build_firmware, target_layout, patch_inputs, PATCH_SERIES, validate_firmware,
                     validate_smoke, validate_smoke_layout, write_json)
from program_target import elf_build_metadata, load_target, readelf_for, target_fingerprint


SUCCESS = 'RHODIUM-LINUX: userspace boot complete; powering off'
FAILURES = ['RHODIUM-LINUX: FAIL', 'Kernel panic', 'Oops:', 'BUG:']
MEMORY_SIZE = 128 * 1024 * 1024
ARTIFACTS = ('fw_jump.elf', 'linux.elf', 'init.elf', 'buildroot.config',
             'linux.config', 'COPYING.Buildroot', 'COPYING.Linux', 'GPL-2.0.Linux',
             'Linux-syscall-note', 'COPYRIGHT.musl', 'COPYING.OpenSBI', 'LICENSE.Rhodium')


def linux_layout(target):
    """Require Linux's boot facilities and the vector/FP userspace profile, independent of core name."""
    layout = target_layout(target)
    required = {'i', 'm', 'a', 'f', 'd', 'c', 'v', 'zba', 'zbb', 'zbs'}
    if (not required <= set(target['extensions']) or target['mabi'] != 'lp64d'
            or target.get('mmu_mode') != 'sv39'
            or not {'m', 's', 'u'} <= set(target.get('privilege_modes', []))):
        raise ValueError('Linux smoke requires RV64 vector/FP, B, LP64D, Sv39, and M/S/U capabilities')
    if layout['ram_size'] < MEMORY_SIZE:
        raise ValueError('Linux smoke requires at least 128 MiB of contiguous boot RAM')
    return layout


def buildroot_architecture(target):
    """Translate the canonical compiler ISA into Buildroot's native configuration fields."""
    linux_layout(target)
    match = re.fullmatch(r'rv64([a-z]+)((?:_[a-z0-9]+)*)', target['march'])
    if not match or not set('imafdcv') <= set(match[1]):
        raise ValueError('cannot project the target compiler ISA into Buildroot')
    extra = list(letter for letter in match[1] if letter not in 'imafdcv')
    extra += [ext for ext in match[2].split('_') if ext and ext not in ('zicsr', 'zifencei')]
    return '\n'.join([
        'BR2_riscv_g=y', 'BR2_RISCV_ISA_RVC=y', 'BR2_RISCV_ISA_RVV=y',
        'BR2_RISCV_ABI_LP64D=y', 'BR2_RISCV_ISA_EXTRA="' + '_'.join(extra) + '"',
    ]) + '\n'


def kernel_payload(image, output, layout, compiler):
    """Wrap the raw Linux Image in a physical-address ELF without adding any executed instructions."""
    data = image.read_bytes()
    if len(data) < 64 or data[48:56] != b'RISCV\0\0\0' or data[56:60] != b'RSC\x05':
        raise ValueError('expected an uncompressed RISC-V Linux Image with its boot header')
    offset, memory_size, flags = struct.unpack_from('<QQQ', data, 8)
    address = layout['next_stage_address']
    if flags & 1 or offset != address - layout['ram_base'] or memory_size < len(data):
        raise ValueError('Linux Image header disagrees with the RV64 boot layout')
    if (address + memory_size > layout['ram_base'] + MEMORY_SIZE
            or address + memory_size > layout['fdt_address']):
        raise ValueError('Linux image including BSS exceeds smoke memory or overlaps the FDT')
    # GNU tools provide normal section/symbol tables expected by FESVR's ELF reader.
    objcopy = str(compiler).removesuffix('gcc') + 'objcopy'
    linker = str(compiler).removesuffix('gcc') + 'ld'
    work = output.parent
    shutil.copyfile(image, work / 'Image')
    subprocess.run([objcopy, '-I', 'binary', '-O', 'elf64-littleriscv', '-B', 'riscv',
                    '--rename-section', '.data=.text,alloc,load,readonly,code,contents',
                    'Image', 'Image.o'], cwd=work, check=True)
    script = work / 'Image.ld'
    script.write_text('/* Generated Linux load envelope; no startup code. */\n'
                      'ENTRY(_kernel_start)\nPHDRS { kernel PT_LOAD FLAGS(7); }\nSECTIONS {\n'
                      f' . = 0x{address:x}; _kernel_start = .;\n'
                      ' .text : { *(.text) } :kernel\n'
                      f' .bss (NOLOAD) : {{ . += {memory_size - len(data)}; }} :kernel\n'
                      '}\n')
    subprocess.run([linker, '-T', str(script), '--build-id=none', '-o', str(output),
                    str(work / 'Image.o')], check=True)
    validate_smoke(layout, output, readelf_for(str(compiler)))
    return dict(image_size=len(data), memory_size=memory_size, load_address=address)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_revision(source):
    if subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True).strip():
        raise ValueError(f'upstream source must remain pristine: {source}')
    return subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()


def validate_bundle(root, target, layout, files):
    if set(files) != set(ARTIFACTS):
        raise ValueError('incomplete Linux artifact inventory')
    for name, checksum in files.items():
        if not (root / name).is_file() or digest(root / name) != checksum:
            raise ValueError(f'incomplete or corrupt Linux artifact: {name}')
    validate_firmware(layout, root / 'fw_jump.elf')
    validate_smoke_layout(layout, root / 'linux.elf')
    check_elf_memory(root / 'linux.elf', target['ram'], require_executable_entry=True)


def require_config(path, values):
    """Reject silently dropped Kconfig requirements before publishing a smoke artifact."""
    text = path.read_text()
    for name, value in values.items():
        line = f'# {name} is not set' if value == 'n' else f'{name}={value}'
        if line not in text.splitlines():
            raise ValueError(f'{path}: smoke configuration requires {line}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--port', type=Path, required=True)
    parser.add_argument('--opensbi-source', type=Path, required=True)
    parser.add_argument('--opensbi-patches', type=Path, default=PATCH_SERIES)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--downloads', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if platform.system() != 'Linux':
        parser.error('Buildroot requires a Linux build host; run linux-smoke-run with a Linux-built archive on macOS')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    try:
        target = load_target(args.target)
        layout = linux_layout(target)
        source, port, opensbi = args.source.resolve(), args.port.resolve(), args.opensbi_source.resolve()
        root = args.output.resolve()
        metadata = elf_build_metadata(target, 'linux')
        inputs = dict(buildroot_revision=source_revision(source), opensbi_revision=source_revision(opensbi),
                      build_spec=metadata['build_spec'], opensbi_patches=patch_inputs(args.opensbi_patches),
                      host=platform.platform(),
                      host_cc=subprocess.check_output(['cc', '--version'], text=True),
                      source=str(source), port=str(port), output=str(root))
        inputs['sources'] = {str(path): digest(path) for path in [
            Path(__file__), Path(__file__).with_name('opensbi.py'),
            Path(__file__).with_name('program_target.py'), Path(__file__).with_name('build.py'),
            *sorted(path for path in port.rglob('*') if path.is_file())]}
        key = target_fingerprint(inputs)
        build = root / 'build' / key
        build.mkdir(parents=True, exist_ok=True)
        (root / 'manifest.json').unlink(missing_ok=True)
        stamp = build / 'complete.json'
        cached = None
        try:
            cached = json.loads(stamp.read_text())
            validate_bundle(root, target, layout, cached['files'])
        except (OSError, ValueError, KeyError):
            cached = None
        if cached is None:
            config = build / 'defconfig'
            config.write_text((port / 'configs/linux_smoke_defconfig').read_text() + buildroot_architecture(target))
            command = ['make', '-C', str(source), f'O={build / "buildroot"}',
                       f'BR2_EXTERNAL={port}', f'BR2_DL_DIR={args.downloads.resolve()}']
            # Never inherit a bare-metal cross prefix or flags from the simulator build.
            env = {k: v for k, v in os.environ.items()
                   if k not in ('CROSS_COMPILE', 'ARCH', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS')}
            with (root / 'build.log').open('w') as log:
                subprocess.run([*command, f'BR2_DEFCONFIG={config}', 'defconfig'], env=env,
                               check=True, stdout=log, stderr=subprocess.STDOUT)
                subprocess.run([*command, f'-j{args.jobs}'], env=env,
                               check=True, stdout=log, stderr=subprocess.STDOUT)
            br = build / 'buildroot'
            compiler = br / 'host/bin/riscv64-buildroot-linux-musl-gcc'
            require_config(br / '.config', dict(BR2_PACKAGE_BUSYBOX='n', BR2_INIT_NONE='y',
                           BR2_PACKAGE_RHODIUM_INIT='y', BR2_STATIC_LIBS='y',
                           BR2_TARGET_ROOTFS_INITRAMFS='y', BR2_RISCV_ISA_RVV='y'))
            program_headers = subprocess.check_output([readelf_for(str(compiler)), '-lW', str(br / 'target/init')], text=True)
            if 'INTERP' in program_headers or 'DYNAMIC' in program_headers:
                raise ValueError('Linux smoke init must be a static executable')
            write_json(root / 'layout.json', layout)
            build_firmware(SimpleNamespace(layout=root / 'layout.json', patches=args.opensbi_patches,
                                           source=opensbi, output=root / 'firmware', compiler=str(compiler), llvm=None))
            shutil.copyfile(root / 'firmware/fw_jump.elf', root / 'fw_jump.elf')
            # Retain the normal Linux image and a generated physical ELF for our ordinary loader.
            image_info = kernel_payload(br / 'images/Image', root / 'linux.elf', layout, compiler)
            shutil.copyfile(br / 'target/init', root / 'init.elf')
            shutil.copyfile(br / '.config', root / 'buildroot.config')
            kernels = list((br / 'build').glob('linux-*/.config'))
            if len(kernels) != 1:
                raise ValueError('expected exactly one configured Linux kernel')
            require_config(kernels[0], dict(CONFIG_MMU='y', CONFIG_RISCV_SBI='y',
                           CONFIG_RISCV_ISA_V='y', CONFIG_FPU='y', CONFIG_HVC_RISCV_SBI='y',
                           CONFIG_BLK_DEV_INITRD='y', CONFIG_BINFMT_ELF='y', CONFIG_SMP='n',
                           CONFIG_CMDLINE_FORCE='y',
                           CONFIG_CMDLINE='"console=hvc0 earlycon=sbi rdinit=/init mem=128M loglevel=4"'))
            shutil.copyfile(kernels[0], root / 'linux.config')
            shutil.copyfile(source / 'COPYING', root / 'COPYING.Buildroot')
            shutil.copyfile(kernels[0].parent / 'COPYING', root / 'COPYING.Linux')
            shutil.copyfile(kernels[0].parent / 'LICENSES/preferred/GPL-2.0', root / 'GPL-2.0.Linux')
            shutil.copyfile(kernels[0].parent / 'LICENSES/exceptions/Linux-syscall-note', root / 'Linux-syscall-note')
            musl_licenses = list((br / 'build').glob('musl-*/COPYRIGHT'))
            if len(musl_licenses) != 1:
                raise ValueError('expected exactly one musl license')
            shutil.copyfile(musl_licenses[0], root / 'COPYRIGHT.musl')
            shutil.copyfile(opensbi / 'COPYING.BSD', root / 'COPYING.OpenSBI')
            shutil.copyfile(Path(__file__).resolve().parents[2] / 'LICENSE', root / 'LICENSE.Rhodium')
            cached = dict(files={name: digest(root / name) for name in ARTIFACTS}, image=image_info,
                          compiler_version=subprocess.check_output([str(compiler), '--version'], text=True))
            validate_bundle(root, target, layout, cached['files'])
            write_json(stamp, cached)
        else:
            (root / 'build.log').write_text(f'Reusing checksum-verified Linux bundle {key}\n')
        checksums = cached['files']
        write_json(root / 'manifest.json', dict(
            suite='linux', target=target, target_fingerprint=target_fingerprint(target), **metadata,
            files=[dict(path=name, sha256=value) for name, value in checksums.items()
                   if name not in ('fw_jump.elf', 'linux.elf')],
            tests=[dict(name='linux-smoke', elf='fw_jump.elf', sha256=checksums['fw_jump.elf'],
                        payload=dict(elf='linux.elf', sha256=checksums['linux.elf']),
                        required_output=[SUCCESS, 'reboot: Power down'], forbidden_output=FAILURES)],
            provenance=inputs | dict(build_key=key, compiler_version=cached['compiler_version'], image=cached['image'])))
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
