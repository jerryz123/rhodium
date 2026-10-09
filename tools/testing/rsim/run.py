#!/usr/bin/env python3
# Builds and runs package-owned rsim component tests without HDL tools.
# SPDX-License-Identifier: Apache-2.0
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
import re
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent


def inventory():
    rows = []
    for line in (HERE / 'fixtures.tsv').read_text().splitlines():
        if line and not line.startswith('#'):
            fields = line.split('\t')
            if not 5 <= len(fields) <= 7:
                raise ValueError('expected 5–7 fixture fields')
            name, group, source, export, driver = fields[:5]
            target = fields[5] if len(fields) > 5 and fields[5] else '-'
            sources = fields[6].split() if len(fields) > 6 else []
            if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", name):
                raise ValueError(f"invalid fixture name: {name}")
            for path in (source, driver, *sources):
                if not (ROOT / path).is_file():
                    raise ValueError(f"{name}: missing fixture file: {path}")
            rows.append(dict(name=name, group=group, source=source, export=export, driver=driver, target=target, sources=sources))
    if len({r['name'] for r in rows}) != len(rows):
        raise ValueError('duplicate rsim fixture name')
    return rows


def run(command, log, timeout=300):
    result = subprocess.run(list(map(str, command)), cwd=ROOT,
                            env=dict(os.environ, PLTCOLLECTS=str(ROOT) + os.pathsep),
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    log.write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f'{log.name}:\n{result.stdout[-8000:]}')
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description="Compile and tick rsim component fixtures")
    parser.add_argument('--group', action='append')
    parser.add_argument('--fixture', action='append')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--work-dir', type=Path)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    rows = inventory()
    unknown_groups = set(args.group or ()) - {row['group'] for row in rows}
    if unknown_groups:
        parser.error('unknown group: ' + ' '.join(sorted(unknown_groups)))
    selected = args.fixture or (os.environ.get('FIXTURE', '') + ' ' + os.environ.get('FIXTURES', '')).split()
    if set(selected) - {r['name'] for r in rows}:
        parser.error('unknown fixture: ' + ' '.join(sorted(set(selected) - {r['name'] for r in rows})))
    rows = [r for r in rows if (not selected or r['name'] in selected) and (not args.group or r['group'] in args.group)]
    if not rows:
        parser.error('no rsim fixtures selected')
    if args.list:
        print('\n'.join(r['name'] for r in rows))
        return
    work = (args.work_dir or Path(tempfile.mkdtemp(prefix='rhodium-components-'))).resolve()
    work.mkdir(parents=True, exist_ok=True)
    try:
        # This directory owns only generated artifacts. Reusing a work directory
        # must not compile stale regions left by a previous model emission.
        for row in rows:
            directory = work / row['name']
            directory.mkdir(parents=True, exist_ok=True)
            for pattern in ('*.cpp', '*.hpp', '*.h', '*.json', 'test'):
                for artifact in directory.glob(pattern):
                    artifact.unlink()
        # Bound each owner group independently: a complete repository run must
        # not share one emission timeout across every architectural workload.
        for group in dict.fromkeys(row['group'] for row in rows):
            emit = [ROOT / 'tools/run-racket.sh', HERE / 'emit.rhm', work]
            for row in rows:
                if row['group'] == group:
                    emit += [row['name'], row['source'], row['export'], row['target']]
            run(emit, work / ('emit-' + group + '.log'), timeout=1800)

        def test(row):
            directory = work / row['name']
            command = shlex.split(os.environ.get('CXX', 'c++')) + ['-std=c++20', '-O2', '-I' + str(HERE), '-I' + str(directory)]
            command += ['-I' + str(parent) for parent in dict.fromkeys((ROOT / source).parent for source in row['sources'])]
            command += list(directory.glob('*.cpp')) + [ROOT / row['driver']] + [ROOT / source for source in row['sources']] + ['-o', directory / 'test']
            run(command, directory / 'build.log')
            output = run([directory / 'test'], directory / 'run.log', timeout=120)
            if output.splitlines().count('PASS') != 1:
                raise RuntimeError(f"{row['name']}: missing completion marker")
            print(f"{row['name']}: PASS", flush=True)

        def attempt(row):
            try:
                test(row)
                return True
            except Exception as error:
                print(f"{row['name']}: FAIL\n{error}", flush=True)
                return False

        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            passed = list(pool.map(attempt, rows))
        if not all(passed):
            raise RuntimeError(f'{passed.count(False)} rsim fixtures failed')
    except BaseException:
        print(f'Rsim component artifacts retained at {work}', flush=True)
        raise
    else:
        if not args.work_dir:
            shutil.rmtree(work)


if __name__ == '__main__':
    main()
