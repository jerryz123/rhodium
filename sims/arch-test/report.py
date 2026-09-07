#!/usr/bin/env python3
# Accounts for every generated ACT ELF using the upstream runner's completion summary.
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def report(elf_dir):
    for name in ('results.json', 'junit.xml'):
        (elf_dir.parent / name).unlink(missing_ok=True)
    elfs = sorted(elf_dir.rglob('*.elf'))
    if not elfs:
        raise ValueError('no ACT ELFs to report')
    summary_path = elf_dir.parent / 'summary.log'
    entries = {}
    for line in summary_path.read_text().splitlines() if summary_path.exists() else []:
        name, _, status = line.partition('  ')
        if name in entries:
            raise ValueError(f'duplicate ACT result: {name}')
        entries[name] = status.strip()
    results = []
    for elf in elfs:
        name = str(elf.relative_to(elf_dir).with_suffix('.log'))
        status = entries.pop(name, '')
        log = elf_dir.parent / 'logs' / name
        cycle_timeout = log.exists() and 'SoC harness simulation timed out' in log.read_text(errors='replace')
        if status.startswith('TIMEOUT') or cycle_timeout:
            outcome = 'timeout'
        elif status.startswith('RVCP-SUMMARY: TEST PASSED '):
            outcome = 'passed'
        elif status.startswith('RVCP-SUMMARY: TEST FAILED '):
            outcome = 'failed'
        else:
            outcome = 'error'
        results.append(dict(name=str(elf.relative_to(elf_dir)), status=outcome,
                            detail=status or 'missing result', log=str(log)))
    if entries:
        raise ValueError('ACT summary contains results outside the generated ELF inventory')
    counts = {state: sum(r['status'] == state for r in results) for state in ('passed', 'failed', 'timeout', 'error')}
    (elf_dir.parent / 'results.json').write_text(json.dumps(dict(summary=counts, tests=results), indent=2) + '\n')
    suite = ET.Element('testsuite', name='arch', tests=str(len(results)), failures=str(counts['failed']),
                       errors=str(counts['error'] + counts['timeout']))
    for result in results:
        case = ET.SubElement(suite, 'testcase', name=result['name'])
        if result['status'] != 'passed':
            ET.SubElement(case, 'failure' if result['status'] == 'failed' else 'error',
                          message=result['status']).text = result['detail']
    ET.ElementTree(suite).write(elf_dir.parent / 'junit.xml', encoding='unicode')
    print(json.dumps(counts))
    return 0 if counts['passed'] == len(elfs) else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf_dir', type=Path)
    raise SystemExit(report(parser.parse_args().elf_dir))
