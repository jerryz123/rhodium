#!/usr/bin/env python3
# Attests and verifies an exact-commit native simulator before cross-job reuse.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import platform
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'sw/build'))
from program_target import load_target, target_fingerprint


def configuration_fingerprint(configuration):
    if not isinstance(configuration, dict) or configuration.get('schema') != 1:
        raise ValueError('unsupported resolved configuration schema')
    return hashlib.sha256(json.dumps(configuration, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('record', 'verify'))
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--soc', required=True)
    parser.add_argument('--target', type=Path)
    parser.add_argument('--backend', choices=('circt', 'verilog', 'rsim'), default='circt')
    parser.add_argument('--variant', choices=('normal', 'trace', 'cosim', 'trace-cosim'), default='normal')
    parser.add_argument('--rtl', type=Path, help='emitted RTL (MLIR or SystemVerilog) used to build this binary (required for recording)')
    parser.add_argument('--configuration', type=Path, help='resolved configuration accompanying ACT payloads')
    args = parser.parse_args()
    if 'trace' in args.variant and args.backend != 'circt':
        parser.error('tracing requires the CIRCT backend')
    root = Path(__file__).resolve().parents[2]
    metadata = dict(commit=subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
                    soc=args.soc, system=platform.system(), machine=platform.machine(),
                    sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest())
    args.target = args.target or args.binary.parent / 'program-target.json'
    try:
        target = load_target(args.target)
        configuration = target['resolved_configuration']
        fingerprint = configuration_fingerprint(configuration)
        if configuration['config'] != args.soc or target['soc'] != args.soc:
            raise ValueError('target configuration describes a different config')
        if target['configuration_fingerprint'] != fingerprint:
            raise ValueError('resolved configuration fingerprint mismatch')
        if args.configuration and configuration_fingerprint(json.loads(args.configuration.read_text())) != fingerprint:
            raise ValueError('ACT payload configuration differs from the simulator')
        metadata.update(target_fingerprint=target_fingerprint(target),
                        configuration_fingerprint=fingerprint, variant=args.variant, backend=args.backend)
        if args.mode == 'record':
            if not args.rtl:
                raise ValueError('recording requires --rtl from the simulator build')
            rtl = args.rtl.read_text()
            if (f'// rhodium-configuration-sha256: {fingerprint}\n' not in rtl
                    or f'// rhodium-harness-variant: {args.variant}\n' not in rtl
                    or f'// rhodium-rtl-backend: {args.backend}\n' not in rtl):
                raise ValueError('emitted RTL configuration, backend, or harness variant differs from the target')
    except (ValueError, KeyError, OSError) as error:
        parser.error(str(error))
    path = args.binary.with_suffix('.json')
    if args.mode == 'record':
        path.write_text(json.dumps(metadata, indent=2) + '\n')
    else:
        recorded = json.loads(path.read_text())
        if any(recorded.get(key) != value for key, value in metadata.items()):
            parser.error('simulator artifact does not match this commit, platform, SoC, binary checksum, target configuration, backend, or harness variant')


if __name__ == '__main__':
    main()
