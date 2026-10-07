#!/usr/bin/env python3
# Groups software builds by generated requirements while preserving every selected SoC run.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'sw/build'))
from program_target import elf_build_spec, load_target, target_fingerprint


def program_matrices(matrix, targets, fdts=None):
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
        if suite == 'opensbi' and (not fdts or soc not in fdts):
            raise ValueError("OpenSBI grouping requires each config's generated device tree")
        options = entry.get('options', {})
        if suite == 'opensbi':
            options = options | dict(fdt_sha256=hashlib.sha256(fdts[soc]).hexdigest())
        spec = elf_build_spec(target, suite, options)
        build_id = suite + '-' + target_fingerprint(spec)
        builds.setdefault(build_id, dict(soc=soc, suite=suite, build_id=build_id, options=options))
        runs.append(entry | dict(build_id=build_id))
    return dict(build={'include': list(builds.values())}, run={'include': runs})


def harness_entries(matrix):
    """Project only shared ELF work; retain every selected config execution."""
    entries = []
    for config in matrix['include']:
        for target in config.get('software_tests', '').split():
            if target == 'isa-smoke':
                entries.append(dict(soc=config['soc'], suite='isa', run_target='isa-smoke-run',
                                    options=dict(isa_selection='smoke', benchmark_mode='target',
                                                 benchmark_selection='single-hart', benchmark_hart_count=None)))
            elif target == 'tiled-mt-benchmark-test':
                entries.append(dict(soc=config['soc'], suite='benchmark', run_target='tiled-mt-benchmark-run',
                                    options=dict(isa_selection=None, benchmark_mode='target', benchmark_selection='multihart',
                                                 benchmark_hart_count=8)))
    return {'include': entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--matrix', required=True, help='selected SoC/suite matrix JSON')
    parser.add_argument('--targets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fdts', type=Path, help='generated per-config OpenSBI device trees')
    parser.add_argument('--harness', action='store_true', help='group the simulator matrix shared harness ELFs')
    args = parser.parse_args()
    try:
        matrix = json.loads(args.matrix)
        if args.harness:
            matrix = harness_entries(matrix)
        if args.harness and not matrix['include']:
            args.output.write_text(json.dumps(dict(build=matrix, run=matrix), indent=2) + '\n')
            return
        targets = {entry['soc']: load_target(args.targets / (entry['soc'] + '.json'))
                   for entry in matrix['include']}
        fdts = {entry['soc']: (args.fdts / (entry['soc'] + '.dtb')).read_bytes()
                for entry in matrix['include'] if entry['suite'] == 'opensbi'} if args.fdts else {}
        args.output.write_text(json.dumps(program_matrices(matrix, targets, fdts), indent=2) + '\n')
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
