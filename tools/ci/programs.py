#!/usr/bin/env python3
# Groups selected native suite builds by generated software requirements while preserving every SoC run.
# SPDX-License-Identifier: Apache-2.0
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'sw/build'))
from program_target import elf_build_spec, load_target, target_fingerprint


def program_matrices(matrix, targets):
    builds, runs = {}, []
    entries = matrix['include']
    if not entries or len({(entry['soc'], entry['suite']) for entry in entries}) != len(entries):
        raise ValueError('native matrix must contain unique, nonempty SoC/suite entries')
    for entry in entries:
        soc, suite = entry['soc'], entry['suite']
        target = targets[soc]
        if target['soc'] != soc:
            raise ValueError('native target descriptor names another SoC')
        # Options are uniform within a CI suite. Builders record their actual
        # options in the archive; consumers compare that complete specification.
        spec = elf_build_spec(target, suite)
        build_id = suite + '-' + target_fingerprint(spec)
        builds.setdefault(build_id, dict(soc=soc, suite=suite, build_id=build_id))
        runs.append(dict(soc=soc, suite=suite, build_id=build_id))
    return dict(build={'include': list(builds.values())}, run={'include': runs})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--matrix', required=True, help='selected SoC/suite matrix JSON')
    parser.add_argument('--targets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        matrix = json.loads(args.matrix)
        targets = {entry['soc']: load_target(args.targets / (entry['soc'] + '.json'))
                   for entry in matrix['include']}
        args.output.write_text(json.dumps(program_matrices(matrix, targets), indent=2) + '\n')
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
