#!/usr/bin/env python3
# Reports retired next-PC prediction accuracy and worst PCs from either core's Perfetto trace.
# SPDX-License-Identifier: Apache-2.0
"""Use native Trace Processor once; never infer prediction results from neighboring PCs."""

import argparse
import csv
import io
import json
import os
from pathlib import Path
import subprocess
import sys


RETIREMENT_LABELS = {
    'core/s5.wb': 'rv5stage',
    'core/s4.wb.slot0': 'rv2wide',
    'core/s4.wb.slot1': 'rv2wide',
}


def query_sql(core='auto', start_cycle=0, end_cycle=None):
    """Aggregate successful retirements without double-counting grouped track descriptors."""
    selection = '' if core == 'auto' else f"AND c.core='{core}'"
    end = '' if end_cycle is None else f"AND CAST(EXTRACT_ARG(s.arg_set_id,'debug.cycle') AS INT)<{end_cycle}"
    labels = ','.join(f"'{label}'" for label in RETIREMENT_LABELS)
    return f"""
WITH labels AS (
  SELECT id, json_extract(EXTRACT_ARG(source_arg_set_id,'description'),'$.label') AS label,
         json_extract(EXTRACT_ARG(source_arg_set_id,'description'),'$.kind') AS kind
  FROM track
  UNION ALL
  SELECT t.id, json_extract(site.value,'$.label'), json_extract(site.value,'$.kind')
  FROM track t, json_each(EXTRACT_ARG(t.source_arg_set_id,'description'),'$.sites') site
), candidates AS (
  SELECT DISTINCT id, CASE label WHEN 'core/s5.wb' THEN 'rv5stage' ELSE 'rv2wide' END AS core
  FROM labels WHERE kind='transfer' AND label IN ({labels})
), selected AS (
  SELECT s.id, c.core,
         EXTRACT_ARG(s.arg_set_id,'debug.pc') AS pc,
         EXTRACT_ARG(s.arg_set_id,'debug.instruction') AS instruction,
         EXTRACT_ARG(s.arg_set_id,'debug.branch_prediction') AS prediction,
         EXTRACT_ARG(s.arg_set_id,'debug.ras_mismatch') AS ras_mismatch
  FROM slice s JOIN candidates c ON c.id=s.track_id
  WHERE s.name!='stall' {selection}
    AND CAST(EXTRACT_ARG(s.arg_set_id,'debug.cycle') AS INT)>={start_cycle} {end}
)
SELECT 'retirement' AS row_type, core, pc, instruction, prediction, ras_mismatch, count(*) AS retirements
FROM selected GROUP BY core,pc,instruction,prediction,ras_mismatch
UNION ALL
SELECT 'error','',name,'Perfetto import error','','',value
FROM stats WHERE severity='error' AND value!=0
UNION ALL
SELECT 'error',c.core,l.label,'Retirement track merged with non-retirement transfers','','',1
FROM candidates c JOIN labels l ON l.id=c.id
WHERE l.kind='transfer' AND (l.label NOT IN ({labels}) OR
  (l.label='core/s5.wb')!=(c.core='rv5stage')) {selection};
"""


def analyze(rows, core='auto', top=20):
    """Validate captures, then count control transfers separately from all retired instructions."""
    counts = dict(retired=0, branches=0, correct=0, mispredictions=0, ras_mismatches=0)
    sites, cores = {}, set()
    for row in rows:
        if row['row_type'] == 'error':
            raise ValueError(f"{row['instruction']}: {row['pc']}")
        prediction, ras = row['prediction'], row['ras_mismatch']
        if prediction not in ('NotBranch', 'Correct', 'Mispredicted') or ras not in ('0', '1'):
            raise ValueError('Missing or invalid branch_prediction/ras_mismatch captures; regenerate the trace with current instrumentation')
        pc, instruction = row['pc'], row['instruction']
        if not pc or not pc.startswith('0x') or not instruction or instruction == '[NULL]':
            raise ValueError('Retirement is missing its PC or instruction capture')
        int(pc, 16)
        count = int(row['retirements'])
        if count <= 0:
            raise ValueError('Invalid retirement count')
        cores.add(row['core'])
        counts['retired'] += count
        counts['ras_mismatches'] += count * int(ras)
        if prediction == 'NotBranch':
            continue
        site = sites.setdefault((pc, instruction), dict(pc=pc, instruction=instruction, branches=0,
                                                       correct=0, mispredictions=0, ras_mismatches=0))
        for target in (counts, site):
            target['branches'] += count
            target['correct' if prediction == 'Correct' else 'mispredictions'] += count
        site['ras_mismatches'] += count * int(ras)
    if not counts['retired']:
        raise ValueError('No successful retirements found in the selected core/cycle window')
    if len(cores) != 1 or not cores <= {'rv5stage', 'rv2wide'}:
        raise ValueError('Trace contains multiple core types; select --core rv5stage or --core rv2wide')
    detected = next(iter(cores))
    if core != 'auto' and detected != core:
        raise ValueError('Unexpected core in retirement results')
    for site in sites.values():
        site['miss_rate_percent'] = 100 * site['mispredictions'] / site['branches']
    ranked = sorted((site for site in sites.values() if site['mispredictions']),
                    key=lambda site: (-site['mispredictions'], -site['branches'], int(site['pc'], 16), site['instruction']))
    return dict(core=detected, **counts,
                accuracy_percent=100 * counts['correct'] / counts['branches'] if counts['branches'] else None,
                mpki=1000 * counts['mispredictions'] / counts['retired'], worst_pcs=ranked[:top])


def format_text(report):
    """Render a compact summary and PCs ranked by absolute next-PC misses."""
    accuracy = 'n/a (no control transfers)' if report['accuracy_percent'] is None else f"{report['accuracy_percent']:.2f}%"
    lines = [f"Core: {report['core']}", f"Retired instructions: {report['retired']:,}",
             f"Control transfers: {report['branches']:,} (conditional branches, JAL, JALR)",
             f"Correct: {report['correct']:,}; mispredicted: {report['mispredictions']:,}",
             f"Effective next-PC accuracy: {accuracy}; MPKI: {report['mpki']:.3f}",
             f"RAS-action mismatches: {report['ras_mismatches']:,} (independent of next-PC accuracy)"]
    if report['worst_pcs']:
        lines += ['', 'PC                  Misses  Executions  Miss rate  RAS mismatches  Instruction']
        for site in report['worst_pcs']:
            lines.append(f"{site['pc']:18}  {site['mispredictions']:6}  {site['branches']:10}  "
                         f"{site['miss_rate_percent']:8.2f}%  {site['ras_mismatches']:14}  {site['instruction']}")
    return '\n'.join(lines)


def nonnegative(value):
    result = int(value)
    if not 0 <= result <= 2**63 - 1:
        raise argparse.ArgumentTypeError('must be a nonnegative signed-64-bit integer')
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path, help='completed .pftrace or .pftrace.gz')
    parser.add_argument('--trace-processor', default=os.environ.get('TRACE_PROCESSOR', 'trace_processor_shell'),
                        help='native executable (defaults to TRACE_PROCESSOR or PATH)')
    parser.add_argument('--core', choices=('auto', 'rv5stage', 'rv2wide'), default='auto')
    parser.add_argument('--start-cycle', type=nonnegative, default=0, help='inclusive retirement cycle')
    parser.add_argument('--end-cycle', type=nonnegative, help='exclusive retirement cycle')
    parser.add_argument('--top', type=nonnegative, default=20, help='maximum number of mispredicted PCs')
    parser.add_argument('--format', choices=('text', 'json'), default='text')
    options = parser.parse_args(argv)
    if options.end_cycle is not None and options.end_cycle <= options.start_cycle:
        parser.error('--end-cycle must exceed --start-cycle')
    try:
        if not options.trace.is_file() or not options.trace.stat().st_size:
            raise ValueError('Trace must be a nonempty file')
        result = subprocess.run([options.trace_processor, 'query', '-f', '-', str(options.trace)],
                                input=query_sql(options.core, options.start_cycle, options.end_cycle),
                                text=True, capture_output=True, check=True)
        report = analyze(csv.DictReader(io.StringIO(result.stdout)), options.core, options.top)
        report['trace'] = str(options.trace.resolve())
        report['start_cycle'], report['end_cycle'] = options.start_cycle, options.end_cycle
        print(json.dumps(report, indent=2) if options.format == 'json' else format_text(report))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        print(f'branch-prediction: {detail}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
