#!/usr/bin/env python3
# Executes every manifest ELF with bounded resources and explicit HTIF completion accounting.
# SPDX-License-Identifier: Apache-2.0
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time
import xml.etree.ElementTree as ET

from program_target import target_fingerprint, validate_target


def execute(test, simulator, root, output, timeout, cycles):
    started = time.monotonic()
    elf = (root / test['elf']).resolve()
    log_path = output / (test['name'] + '.log')
    command = [str(simulator), '+permissive', f'+max-cycles={cycles}', '+permissive-off', str(elf)]
    status = 'error'
    try:
        if hashlib.sha256(elf.read_bytes()).hexdigest() != test['sha256']:
            raise ValueError(f'ELF checksum mismatch: {elf}')
        with log_path.open('w') as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                status = 'timeout'
            else:
                lines = log_path.read_text(errors='replace').splitlines()
                if any('SoC harness simulation timed out' in line for line in lines):
                    status = 'timeout'
                elif code == 0 and 'SoC harness simulation passed' in lines:
                    status = 'passed'
                else:
                    status = 'failed'
    except (OSError, ValueError) as error:
        log_path.write_text(str(error) + '\n')
    result = dict(name=test['name'], status=status, seconds=time.monotonic() - started,
                  log=str(log_path), command=command)
    print(f'{status}: {test["name"]}', flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--simulator', type=Path, required=True)
    parser.add_argument('--simulator-metadata', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=1)
    parser.add_argument('--timeout', type=float, default=300)
    parser.add_argument('--max-cycles', type=int, default=10000000)
    args = parser.parse_args()
    if min(args.jobs, args.timeout, args.max_cycles) <= 0:
        parser.error('jobs, timeout, and max-cycles must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ('results.json', 'junit.xml'):
        (args.output / name).unlink(missing_ok=True)
    manifest = json.loads(args.manifest.read_text())
    if 'target' in manifest:
        try:
            target = validate_target(manifest['target'])
        except ValueError as error:
            parser.error(str(error))
        expected_fingerprint = target_fingerprint(target)
        if manifest.get('target_fingerprint') != expected_fingerprint:
            parser.error('manifest target fingerprint is missing or invalid')
        if not args.simulator_metadata:
            parser.error('a target-bound manifest requires --simulator-metadata')
        simulator_metadata = json.loads(args.simulator_metadata.read_text())
        if (simulator_metadata.get('target_fingerprint') != expected_fingerprint
                or simulator_metadata.get('soc') != target['soc']
                or simulator_metadata.get('sha256') != hashlib.sha256(args.simulator.read_bytes()).hexdigest()):
            parser.error('workload target does not match simulator attestation')
    tests = manifest['tests']
    names = [test['name'] for test in tests]
    if not tests or len(set(names)) != len(names) or any(Path(n).name != n or n in ('.', '..') for n in names):
        parser.error('manifest must contain nonempty, unique, simple test names')
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        results = list(executor.map(lambda test: execute(test, args.simulator.resolve(), args.manifest.parent,
                                                       args.output, args.timeout, args.max_cycles), tests))
    summary = {state: sum(r['status'] == state for r in results) for state in ('passed', 'failed', 'timeout', 'error')}
    (args.output / 'results.json').write_text(json.dumps(dict(summary=summary, tests=results), indent=2) + '\n')
    suite = ET.Element('testsuite', name=manifest['suite'], tests=str(len(results)),
                       failures=str(summary['failed']), errors=str(summary['error'] + summary['timeout']))
    for result in results:
        case = ET.SubElement(suite, 'testcase', name=result['name'], time=str(result['seconds']))
        if result['status'] != 'passed':
            ET.SubElement(case, 'failure' if result['status'] == 'failed' else 'error',
                          message=result['status']).text = f'See {result["log"]}'
    ET.ElementTree(suite).write(args.output / 'junit.xml', encoding='unicode')
    print(json.dumps(summary))
    return 0 if summary['passed'] == len(tests) else 1


if __name__ == '__main__':
    raise SystemExit(main())
