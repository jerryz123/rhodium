#!/usr/bin/env python3
# Attests and verifies an exact-commit native simulator before cross-job reuse.
import argparse
import hashlib
import json
import platform
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('record', 'verify'))
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--soc', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    metadata = dict(commit=subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
                    soc=args.soc, system=platform.system(), machine=platform.machine(),
                    sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest())
    path = args.binary.with_suffix('.json')
    if args.mode == 'record':
        path.write_text(json.dumps(metadata, indent=2) + '\n')
    elif json.loads(path.read_text()) != metadata:
        parser.error('simulator artifact does not match this commit, platform, SoC, or binary checksum')


if __name__ == '__main__':
    main()
