#!/usr/bin/env python3
# Executes every manifest ELF with bounded resources and explicit HTIF completion accounting.
# SPDX-License-Identifier: Apache-2.0
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'sw/build'))
from program_target import target_fingerprint, validate_target


LITMUS_STATE_LINE = re.compile(r'^\s*(\d+)\s*[*:]>(.*)$')
LITMUS_HISTOGRAM = re.compile(r'^Histogram \((\d+) states\)$')
ACTIVE_PROCESSES = set()
ACTIVE_LOCK = threading.Lock()
CANCELLED = threading.Event()


def check_litmus_states(test, lines):
    allowed = test['litmus_allowed_states']
    minimum = test.get('litmus_min_samples')
    if not isinstance(allowed, list) or not allowed or any(not isinstance(state, str) for state in allowed):
        raise ValueError(f'{test["name"]}: invalid litmus model states')
    if not isinstance(minimum, int) or isinstance(minimum, bool) or minimum <= 0:
        raise ValueError(f'{test["name"]}: invalid litmus sample minimum')
    normalize = lambda state: re.sub(r'\[([A-Za-z_][A-Za-z_0-9]*)\]', r'\1',
                                     ' '.join(state.split()))
    permitted = {normalize(state) for state in allowed}
    seen = []
    samples = 0
    headers = [(index, int(match[1])) for index, line in enumerate(lines)
               if (match := LITMUS_HISTOGRAM.fullmatch(line))]
    if len(headers) != 1 or headers[0][1] < 1:
        return 'missing, duplicate, or empty litmus histogram'
    start, count = headers[0]
    if len(lines) < start + 1 + count:
        return 'truncated litmus histogram'
    for line in lines[start + 1:start + 1 + count]:
        match = LITMUS_STATE_LINE.fullmatch(line)
        if not match:
            return f'malformed litmus histogram state: {line}'
        samples += int(match[1])
        state = normalize(match[2].strip())
        if state not in permitted:
            seen.append(state)
    if seen:
        return f'outcomes forbidden by pinned Herd model: {seen}'
    if samples < minimum:
        return f'only {samples} litmus outcomes reported; expected at least {minimum}'
    return None


def execute(test, simulator, root, output, timeout, cycles):
    started = time.monotonic()
    elf = (root / test['elf']).resolve()
    log_path = output / (test['name'] + '.log')
    command = [str(simulator)]
    if 'harts' in test:
        command.append('+boot-harts=' + ','.join(str(hart) for hart in test['harts']))
    command += ['+permissive', f'+max-cycles={cycles}', '+permissive-off', str(elf)]
    status = 'error'
    reason = None
    try:
        if hashlib.sha256(elf.read_bytes()).hexdigest() != test['sha256']:
            raise ValueError(f'ELF checksum mismatch: {elf}')
        with log_path.open('w') as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            with ACTIVE_LOCK:
                ACTIVE_PROCESSES.add(process.pid)
            try:
                if CANCELLED.is_set():
                    os.killpg(process.pid, signal.SIGKILL)
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                status = 'timeout'
                reason = 'wall-timeout'
            else:
                text = log_path.read_text(errors='replace')
                lines = text.splitlines()
                if any('SoC harness simulation timed out' in line for line in lines):
                    status = 'timeout'
                    reason = 'cycle-timeout'
                elif code == 0 and 'SoC harness simulation passed' in text:
                    required = test.get('required_output', [])
                    forbidden = test.get('forbidden_output', [])
                    if (not isinstance(required, list) or not isinstance(forbidden, list)
                            or any(not isinstance(marker, str) or not marker for marker in required + forbidden)):
                        raise ValueError(f'{test["name"]}: invalid output contract')
                    missing = [marker for marker in required if marker not in text]
                    present = [marker for marker in forbidden if marker in text]
                    if missing or present:
                        status = 'failed'
                        reason = f'missing output {missing}; forbidden output {present}'
                    elif 'litmus_allowed_states' in test:
                        reason = check_litmus_states(test, lines)
                        status = 'failed' if reason else 'passed'
                    else:
                        status = 'passed'
                else:
                    status = 'failed'
            finally:
                with ACTIVE_LOCK:
                    ACTIVE_PROCESSES.discard(process.pid)
    except (OSError, ValueError) as error:
        log_path.write_text(str(error) + '\n')
    result = dict(name=test['name'], status=status, seconds=time.monotonic() - started,
                  log=str(log_path), command=command)
    if reason:
        result['reason'] = reason
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
    parser.add_argument('--shard-index', type=int, default=0)
    parser.add_argument('--shard-count', type=int, default=1)
    parser.add_argument('--resume', action='store_true', help='reuse completed results only for an identical run')
    args = parser.parse_args()
    if min(args.jobs, args.timeout, args.max_cycles) <= 0:
        parser.error('jobs, timeout, and max-cycles must be positive')
    if args.shard_count <= 0 or not 0 <= args.shard_index < args.shard_count:
        parser.error('shard-index must be in [0, shard-count)')
    args.output.mkdir(parents=True, exist_ok=True)
    manifest_bytes = args.manifest.read_bytes()
    manifest = json.loads(manifest_bytes)
    target = None
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
    for test in tests:
        if 'harts' not in test:
            continue
        harts = test['harts']
        if (target is None or not isinstance(harts, list) or not harts
                or any(not isinstance(hart, int) or isinstance(hart, bool) for hart in harts)
                or harts != sorted(harts) or len(harts) != len(set(harts))
                or any(hart not in target['harts'] for hart in harts)):
            parser.error(f'{test["name"]}: harts must be a canonical nonempty subset of the target')
    tests = [test for index, test in enumerate(sorted(tests, key=lambda test: test['name']))
             if index % args.shard_count == args.shard_index]
    if not tests:
        parser.error('selected shard is empty')
    manifest_sha256 = hashlib.sha256(manifest_bytes).hexdigest()
    run_fingerprint = hashlib.sha256(json.dumps(dict(
        manifest_sha256=manifest_sha256,
        simulator_sha256=hashlib.sha256(args.simulator.read_bytes()).hexdigest(),
        timeout=args.timeout, max_cycles=args.max_cycles),
        sort_keys=True).encode()).hexdigest()
    identity = hashlib.sha256(json.dumps(dict(
        run_fingerprint=run_fingerprint,
        shard_index=args.shard_index, shard_count=args.shard_count),
        sort_keys=True).encode()).hexdigest()
    result_path = args.output / 'results.json'
    saved = {}
    if args.resume and result_path.exists():
        previous = json.loads(result_path.read_text())
        if previous.get('identity') != identity:
            parser.error('existing results have a different run identity; select a fresh output directory')
        saved = {result['name']: result for result in previous['tests']}
        expected = {test['name'] for test in tests}
        if (not set(saved) <= expected or len(saved) != len(previous['tests'])
                or any(result['status'] not in ('passed', 'failed', 'timeout', 'error')
                       or not Path(result['log']).is_file() for result in saved.values())):
            parser.error('existing results contain an invalid or missing completed case')
    (args.output / 'junit.xml').unlink(missing_ok=True)

    def checkpoint():
        results = [saved[name] for name in sorted(saved)]
        summary = {state: sum(result['status'] == state for result in results)
                   for state in ('passed', 'failed', 'timeout', 'error')}
        pending = sorted(test['name'] for test in tests if test['name'] not in saved)
        payload = dict(identity=identity, manifest_sha256=manifest_sha256,
                       run_fingerprint=run_fingerprint,
                       complete=not pending, shard_index=args.shard_index,
                       shard_count=args.shard_count, pending=pending, summary=summary,
                       build_failures=manifest.get('build_failures', []), tests=results)
        temporary = result_path.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(payload, indent=2) + '\n')
        temporary.replace(result_path)
        return results, summary

    checkpoint()
    pending_tests = [test for test in tests if test['name'] not in saved]
    def interrupt(_signum, _frame):
        CANCELLED.set()
        with ACTIVE_LOCK:
            active = list(ACTIVE_PROCESSES)
        for pid in active:
            try:
                os.killpg(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        raise KeyboardInterrupt

    previous_sigint = signal.signal(signal.SIGINT, interrupt)
    previous_sigterm = signal.signal(signal.SIGTERM, interrupt)
    try:
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            remaining = iter(pending_tests)
            futures = {}

            def submit(test):
                future = executor.submit(execute, test, args.simulator.resolve(), args.manifest.parent,
                                         args.output, args.timeout, args.max_cycles)
                futures[future] = test['name']

            for _ in range(min(args.jobs, len(pending_tests))):
                submit(next(remaining))
            while futures:
                future = next(as_completed(futures))
                del futures[future]
                result = future.result()
                saved[result['name']] = result
                checkpoint()
                next_test = next(remaining, None)
                if next_test is not None:
                    submit(next_test)
    except KeyboardInterrupt:
        print(f'interrupted: {len(saved)} of {len(tests)} completed; resume with --resume', file=sys.stderr)
        return 130
    finally:
        signal.signal(signal.SIGINT, previous_sigint)
        signal.signal(signal.SIGTERM, previous_sigterm)
    results, summary = checkpoint()
    suite = ET.Element('testsuite', name=manifest['suite'], tests=str(len(results)),
                       failures=str(summary['failed']), errors=str(summary['error'] + summary['timeout']))
    for result in results:
        case = ET.SubElement(suite, 'testcase', name=result['name'], time=str(result['seconds']))
        if result['status'] != 'passed':
            ET.SubElement(case, 'failure' if result['status'] == 'failed' else 'error',
                          message=result['status']).text = result.get('reason', f'See {result["log"]}')
    ET.ElementTree(suite).write(args.output / 'junit.xml', encoding='unicode')
    print(json.dumps(summary))
    return 0 if summary['passed'] == len(tests) and not manifest.get('build_failures') else 1


if __name__ == '__main__':
    raise SystemExit(main())
