# Tests retirement selection, prediction denominators, ranked PCs, and strict trace failures.
# SPDX-License-Identifier: Apache-2.0
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sqlite3
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / 'branch-prediction.py'
spec = importlib.util.spec_from_file_location('branch_prediction', SCRIPT)
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class PredictionTest(unittest.TestCase):
    def setUp(self):
        self.database = sqlite3.connect(':memory:')
        self.addCleanup(self.database.close)
        self.database.executescript('''
          CREATE TABLE track(id INTEGER,source_arg_set_id INTEGER);
          CREATE TABLE slice(id INTEGER,track_id INTEGER,arg_set_id INTEGER,name TEXT);
          CREATE TABLE stats(name TEXT,severity TEXT,value INTEGER);
        ''')
        self.args = {}
        self.database.create_function('EXTRACT_ARG', 2, lambda identifier, key: self.args.get(identifier, {}).get(key))
        self.track(1, 'core/s5.wb')
        self.track(2, 'core/s4.wb.slot0')
        self.track(3, 'core/s4.wb.slot1')
        self.track(4, 'core/s4.wb.deferred')
        self.track(5, 'core/s3.mem.slot0')

    def track(self, identifier, label, sites=None):
        description = dict(label=label, kind='transfer') if sites is None else dict(sites=sites)
        self.args[-identifier] = {'description': json.dumps(description)}
        self.database.execute('INSERT INTO track VALUES(?,?)', (identifier, -identifier))

    def event(self, track=1, prediction='NotBranch', ras=0, pc='0x0000000080000100', instruction='addi a0, a0, 1', cycle=1, name='instruction'):
        identifier = len([key for key in self.args if key > 0]) + 1
        self.args[identifier] = {'debug.pc': pc, 'debug.instruction': instruction,
                                 'debug.branch_prediction': prediction, 'debug.ras_mismatch': ras,
                                 'debug.cycle': cycle}
        self.database.execute('INSERT INTO slice VALUES(?,?,?,?)', (identifier, track, identifier, name))

    def rows(self, core='auto', start=0, end=None):
        cursor = self.database.execute(analysis.query_sql(core, start, end))
        names = [column[0] for column in cursor.description]
        return [dict(zip(names, ('' if value is None else str(value) for value in row))) for row in cursor]

    def test_both_cores_share_denominators_and_rank_absolute_misses(self):
        for core, tracks in [('rv5stage', [1]), ('rv2wide', [2, 3])]:
            with self.subTest(core=core):
                self.database.execute('DELETE FROM slice')
                for index in range(8):
                    self.event(track=tracks[index % len(tracks)])
                for index, (pc, result, ras) in enumerate([
                    ('0x80000110', 'Correct', 1), ('0x80000110', 'Correct', 0),
                    ('0x80000110', 'Mispredicted', 0), ('0x80000110', 'Mispredicted', 1),
                    ('0x80000120', 'Mispredicted', 0)]):
                    self.event(track=tracks[index % len(tracks)], prediction=result, ras=ras, pc=pc,
                               instruction='bne a0, a1, pc - 4')
                self.event(track=tracks[0], ras=1)  # Nonbranch RAS repair is not a next-PC miss.
                self.event(track=tracks[0], name='stall', prediction='Mispredicted')
                self.event(track=4, prediction='Mispredicted')
                self.event(track=5, prediction='Mispredicted')
                report = analysis.analyze(self.rows())
                self.assertEqual((report['core'], report['retired'], report['branches']), (core, 14, 5))
                self.assertEqual((report['correct'], report['mispredictions'], report['ras_mismatches']), (2, 3, 3))
                self.assertEqual(report['accuracy_percent'], 40)
                self.assertAlmostEqual(report['mpki'], 3000 / 14)
                self.assertEqual([site['mispredictions'] for site in report['worst_pcs']], [2, 1])
                self.assertEqual([site['miss_rate_percent'] for site in report['worst_pcs']], [50, 100])
                self.assertIn('Effective next-PC accuracy: 40.00%', analysis.format_text(report))

    def test_half_open_window_counts_dual_retirement_once_per_instruction(self):
        for cycle in [4, 5, 6]:
            self.event(track=2, cycle=cycle, prediction='Correct')
            self.event(track=3, cycle=cycle, prediction='Mispredicted')
        report = analysis.analyze(self.rows(start=5, end=6), top=1)
        self.assertEqual((report['retired'], report['branches'], report['accuracy_percent']), (2, 2, 50))
        self.assertEqual(len(report['worst_pcs']), 1)

    def test_no_branches_has_undefined_accuracy_not_perfect_accuracy(self):
        self.event()
        report = analysis.analyze(self.rows())
        self.assertIsNone(report['accuracy_percent'])
        self.assertEqual((report['mpki'], report['worst_pcs']), (0, []))
        self.assertIn('n/a', analysis.format_text(report))

    def test_mixed_core_trace_requires_selection(self):
        self.event(track=1)
        self.event(track=2)
        with self.assertRaisesRegex(ValueError, 'multiple core types'):
            analysis.analyze(self.rows())
        self.assertEqual(analysis.analyze(self.rows('rv2wide'))['retired'], 1)

    def test_grouped_retirement_tracks_do_not_duplicate_counts(self):
        self.track(6, None, sites=[dict(label='core/s4.wb.slot0', kind='transfer'),
                                  dict(label='core/s4.wb.slot1', kind='transfer')])
        self.event(track=6, prediction='Mispredicted')
        self.assertEqual(analysis.analyze(self.rows())['retired'], 1)

    def test_mixing_completion_with_retirement_in_one_track_is_rejected(self):
        self.track(6, None, sites=[dict(label='core/s4.wb.slot0', kind='transfer'),
                                  dict(label='core/s4.wb.deferred', kind='transfer')])
        self.event(track=6)
        with self.assertRaisesRegex(ValueError, 'merged with non-retirement'):
            analysis.analyze(self.rows())

    def test_core_selection_cannot_disambiguate_a_cross_core_shared_track(self):
        self.track(6, None, sites=[dict(label='core/s4.wb.slot0', kind='transfer'),
                                  dict(label='core/s5.wb', kind='transfer')])
        self.event(track=6)
        with self.assertRaisesRegex(ValueError, 'merged with non-retirement'):
            analysis.analyze(self.rows('rv2wide'))

    def test_missing_unknown_or_invalid_capture_fails_closed(self):
        for prediction, ras in [(None, 0), ('0x3', 0), ('Correct', None), ('Correct', 2)]:
            self.database.execute('DELETE FROM slice')
            self.event(prediction=prediction, ras=ras)
            with self.assertRaisesRegex(ValueError, 'regenerate the trace'):
                analysis.analyze(self.rows())
        self.database.execute('DELETE FROM slice')
        with self.assertRaisesRegex(ValueError, 'No successful retirements'):
            analysis.analyze(self.rows())

    def test_import_errors_are_not_reported_as_accuracy(self):
        self.event()
        self.database.execute("INSERT INTO stats VALUES('track_event_parser_errors','error',1)")
        with self.assertRaisesRegex(ValueError, 'Perfetto import error'):
            analysis.analyze(self.rows())

    def test_native_cli_imports_once_and_json_is_machine_readable(self):
        self.event(prediction='Correct')
        rows = self.rows()
        output = io.StringIO()
        import csv
        writer = csv.DictWriter(output, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / 'test.pftrace.gz'
            trace.write_bytes(b'test fixture')
            stdout = io.StringIO()
            with patch.object(analysis.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, output.getvalue(), '')) as run:
                with contextlib.redirect_stdout(stdout):
                    self.assertEqual(analysis.main([str(trace), '--trace-processor', '/native/importer', '--format', 'json']), 0)
                self.assertEqual(run.call_count, 1)
                self.assertEqual(run.call_args.args[0], ['/native/importer', 'query', '-f', '-', str(trace)])
            self.assertEqual(json.loads(stdout.getvalue())['accuracy_percent'], 100)


if __name__ == '__main__':
    unittest.main()
