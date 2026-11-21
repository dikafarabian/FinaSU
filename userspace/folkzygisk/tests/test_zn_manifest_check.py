import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class ZnManifestCheckTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(directory.cleanup)
        cls.checker = Path(directory.name) / "zn-manifest-check"
        source = Path(__file__).resolve().parents[1] / "diagnostics/zn_manifest_check.c"
        subprocess.run(
            [os.environ.get("CC", "cc"), "-std=c17", "-Wall", "-Wextra", "-Werror",
             str(source), "-o", str(cls.checker)],
            check=True,
        )

    def run_checker(self, manifest):
        return subprocess.run([self.checker], input=manifest, capture_output=True)

    def test_counts_declarations_without_requiring_a_trailing_newline(self):
        result = self.run_checker(b"# comment\r\n\nname=sample lib/a.so\npath=/system/bin/sample companion lib/b.so")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"entries=2 companion=1\n")

    def test_rejects_unsupported_declarations(self):
        for line in (b"name=", b"path=relative lib/a.so", b"name=x ../a.so", b"name=x /a.so",
                     b"name=x lib//a.so", b"name=x companion", b"name=x lib/a.so more",
                     b"name=x lib/a.so\0extra", b"#" * 4096):
            with self.subTest(line=line[:80]):
                result = self.run_checker(b"\n" + line)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, b"")
                self.assertIn(b"line 2", result.stderr)
