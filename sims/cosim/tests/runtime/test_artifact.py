# SPDX-License-Identifier: Apache-2.0
import importlib.util
import json
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'cosim_artifact', Path(__file__).resolve().parents[2] / 'runtime/artifact.py')
artifact = importlib.util.module_from_spec(spec)
spec.loader.exec_module(artifact)


class RuntimeArtifactTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        for name in artifact.FILES[1:]:
            (self.root / name).write_bytes(name.encode())
        (self.root / 'link-flags.txt').write_text(
            ' '.join(str(self.root / name) for name in artifact.FILES[1:]) + '\n')

    def test_exact_runtime_is_reusable(self):
        artifact.record(self.root)
        artifact.verify(self.root)

    def test_make_consumes_prebuilt_libraries_without_cmake(self):
        artifact.record(self.root)
        result = subprocess.run(['make', '-C', str(artifact.ROOT / 'sims'),
                                 'sail-cosim-build', f'PREBUILT_COSIM_DIR={self.root}'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('artifact.py verify', result.stdout)
        self.assertNotIn('cmake', result.stdout)

    def test_corrupt_or_missing_files_fail(self):
        for name in artifact.FILES:
            with self.subTest(name=name):
                original = (self.root / name).read_bytes()
                artifact.record(self.root)
                (self.root / name).write_bytes(original + b'corrupt')
                with self.assertRaises((ValueError, FileNotFoundError)):
                    artifact.verify(self.root)
                (self.root / name).unlink()
                with self.assertRaises(FileNotFoundError):
                    artifact.verify(self.root)
                (self.root / name).write_bytes(original)

    def test_source_revision_platform_and_paths_are_checked(self):
        artifact.record(self.root)
        manifest = json.loads((self.root / 'artifact.json').read_text())
        for field in ('commit', 'sources', 'machine', 'system', 'workspace', 'directory'):
            with self.subTest(field=field), patch.object(
                    artifact, 'environment', return_value=artifact.environment(self.root) | {field: 'other'}):
                with self.assertRaises(ValueError):
                    artifact.verify(self.root)
        (self.root / 'artifact.json').write_text(json.dumps(manifest | {'files': {}}))
        with self.assertRaises(ValueError):
            artifact.verify(self.root)

    def test_missing_or_omitted_link_archives_fail_at_publication(self):
        (self.root / 'link-flags.txt').write_text(str(self.root / 'missing.a'))
        with self.assertRaisesRegex(ValueError, 'missing'):
            artifact.record(self.root)
        (self.root / 'link-flags.txt').write_text(str(self.root / artifact.FILES[1]))
        with self.assertRaisesRegex(ValueError, 'omit'):
            artifact.record(self.root)
