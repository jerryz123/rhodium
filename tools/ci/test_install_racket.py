# Tests installer cache reuse, checksum rejection, and CI installation compatibility.
# SPDX-License-Identifier: Apache-2.0

import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]


class RacketInstallerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cache = self.root / "cache with spaces"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.trace = self.root / "trace"
        self.source = self.root / "installer.sh"
        self.source.write_text('#!/bin/sh\nprintf "install:%s\\n" "$*" >> "$RACKET_TEST_TRACE"\n')
        script = (REPO / "tools/ci/install-racket.sh").read_text()
        pin = script.split('installer_sha256="', 1)[1].split('"', 1)[0]
        self.script = self.root / "setup.sh"
        self.script.write_text(script.replace(pin, hashlib.sha256(self.source.read_bytes()).hexdigest()))
        stubs = {
            "uname": 'if [ "$1" = -s ]; then echo Linux; else echo x86_64; fi',
            "curl": 'echo download >> "$RACKET_TEST_TRACE"\nfor arg; do destination="$arg"; done\ncp "$RACKET_TEST_SOURCE" "$destination"',
            "sudo": 'exec "$@"',
            "racket": 'echo "${RACKET_TEST_VERSION:-9.2}"',
            "sha256sum": 'exec shasum -a 256 "$@"',
        }
        for name, body in stubs.items():
            path = self.bin / name
            path.write_text("#!/bin/sh\nset -eu\n" + body + "\n")
            path.chmod(0o755)
        self.env = {
            **os.environ,
            "PATH": f"{self.bin}{os.pathsep}{os.environ['PATH']}",
            "RACKET_TEST_TRACE": str(self.trace),
            "RACKET_TEST_SOURCE": str(self.source),
        }

    def run_setup(self, version="9.2"):
        return subprocess.run(["bash", str(self.script), version, str(self.cache)],
                              env=self.env, capture_output=True, text=True)

    def events(self):
        return self.trace.read_text().splitlines() if self.trace.exists() else []

    def test_miss_downloads_once_and_hit_preserves_install_paths(self):
        for _ in range(2):
            result = self.run_setup()
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.events(), ["download",
                                        "install:--create-dir --unix-style --dest /usr/",
                                        "install:--create-dir --unix-style --dest /usr/"])
        self.assertEqual((self.cache / "racket-9.2-x86_64-linux-buster-cs.sh").read_bytes(),
                         self.source.read_bytes())

    def test_corrupt_cache_is_never_executed_or_silently_replaced(self):
        self.cache.mkdir()
        (self.cache / "racket-9.2-x86_64-linux-buster-cs.sh").write_text("corrupt")
        self.assertNotEqual(self.run_setup().returncode, 0)
        self.assertEqual(self.events(), [])

    def test_bad_download_is_not_promoted_to_cache_or_executed(self):
        self.source.write_text("corrupt")
        self.assertNotEqual(self.run_setup().returncode, 0)
        self.assertEqual(self.events(), ["download"])
        self.assertFalse((self.cache / "racket-9.2-x86_64-linux-buster-cs.sh").exists())

    def test_changed_version_requires_a_matching_pin(self):
        self.assertNotEqual(self.run_setup("9.3").returncode, 0)
        self.assertEqual(self.events(), [])

    def test_installed_runtime_must_match_requested_version(self):
        self.env["RACKET_TEST_VERSION"] = "9.3"
        result = self.run_setup()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Racket version mismatch", result.stderr)


if __name__ == "__main__":
    unittest.main()
