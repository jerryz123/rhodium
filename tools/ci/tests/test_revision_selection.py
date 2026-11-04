# SPDX-License-Identifier: Apache-2.0
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.ci.actions import changed_paths


class RevisionSelection(unittest.TestCase):
    def test_explicit_trees_and_unavailable_revisions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def git(*args):
                return subprocess.check_output(['git', *args], cwd=root, text=True).strip()
            git('init', '-q', '-b', 'main')
            for name, value in [('user.name', 'Test'), ('user.email', 'test@example.invalid'),
                                ('commit.gpgsign', 'false'), ('core.hooksPath', '/dev/null')]:
                git('config', name, value)
            (root / '.moon').mkdir()
            (root / '.moon/workspace.yml').write_text("projects: { test: '.' }\n")
            for path in ('old name', 'deleted'):
                (root / path).write_text(path)
            git('add', '.')
            git('commit', '-qm', 'base')
            base = git('rev-parse', 'HEAD')
            git('mv', 'old name', 'new name')
            git('rm', '-q', 'deleted')
            git('commit', '-qm', 'move and delete')
            head = git('rev-parse', 'HEAD')
            git('switch', '-qc', 'sibling', base)
            (root / 'other').touch()
            git('add', 'other')
            git('commit', '-qm', 'other change')
            sibling = git('rev-parse', 'HEAD')
            (root / 'uncommitted').touch()
            # Inside ci-contracts, moon exports the parent workspace to child commands.
            with patch.dict(os.environ, {'MOON_WORKSPACE_ROOT': str(root)}), \
                    patch('tools.ci.actions.ROOT', root), patch('tools.ci.moon.ROOT', root):
                moved = {'old name', 'new name', 'deleted'}
                self.assertEqual(set(changed_paths(base, head)), moved)
                self.assertEqual(set(changed_paths(head, sibling)), moved | {'other'})
                self.assertEqual(changed_paths(head, head), [])
                self.assertIsNone(changed_paths('0' * 40, head))
                self.assertIsNone(changed_paths('1' * 40, head))
                # A gitlink need not be initialized to affect its consumers.
                for commit in (base, head):
                    git('update-index', '--add', '--cacheinfo', f'160000,{commit},vendor/source')
                    git('commit', '-qm', 'pin source')
                    self.assertEqual(changed_paths('HEAD^', 'HEAD'), ['vendor/source'])
                git('rm', '--cached', '-q', 'vendor/source')
                git('commit', '-qm', 'remove pin')
                self.assertEqual(changed_paths('HEAD^', 'HEAD'), ['vendor/source'])
