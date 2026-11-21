import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "zygiskd/src/zn_plan.c"
PROBE = ROOT / "zygiskd/src/zn_plan_probe.c"

ELF_BYTES = {
    "x86_64": b"\x7fELF\x02\x01\x01" + b"\x00" * 9 + b"\x03\x00\x3e\x00" + b"\x00" * 44,
    "aarch64": b"\x7fELF\x02\x01\x01" + b"\x00" * 9 + b"\x03\x00\xb7\x00" + b"\x00" * 44,
}


class ZnPlanTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        cls.base = Path(cls.directory.name)
        cls.probe = cls.base / "probe"
        subprocess.run(
            [os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
             "-D_GNU_SOURCE", "-I", str(ROOT / "zygiskd/src"),
             str(SOURCE), str(PROBE), "-o", str(cls.probe)],
            check=True,
        )
        machine = subprocess.run(["uname", "-m"], capture_output=True, text=True, check=True).stdout.strip()
        cls.library = ELF_BYTES["aarch64" if machine in ("aarch64", "arm64") else "x86_64"]

    def setUp(self):
        self.root = self.base / self.id().rsplit(".", 1)[-1]
        shutil.rmtree(self.root, ignore_errors=True)
        self.root.mkdir()

    def module(self, name, manifest, libraries=("lib/a.so",), disabled=False):
        directory = self.root / name
        directory.mkdir()
        if manifest is not None:
            (directory / "zn_modules.txt").write_text(manifest)
        for library in libraries:
            path = directory / library
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(self.library)
        if disabled:
            (directory / "disable").write_text("")
        return directory

    def collect(self, name="adbd", exe="/system/bin/adbd"):
        result = subprocess.run([self.probe, str(self.root), name, exe], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.splitlines()

    def test_matches_name_and_path_targets_only(self):
        self.module("alpha", "name=adbd lib/a.so\npath=/apex/com.android.adbd/bin/adbd lib/a.so\n")
        self.module("beta", "name=installd lib/a.so\n")
        self.assertEqual(self.collect(), ["count=1", f"module={self.root}/alpha/lib/a.so companion=0"])

    def test_reads_companion_flag(self):
        self.module("alpha", "name=adbd companion lib/a.so\n")
        self.assertEqual(self.collect()[1].endswith("companion=1"), True)

    def test_skips_disabled_modules(self):
        self.module("alpha", "name=adbd lib/a.so\n", disabled=True)
        self.assertEqual(self.collect(), ["count=0"])

    def test_rejects_malformed_manifest_entirely(self):
        for manifest in (
            "name=adbd ../escape.so\n",
            "name=adbd lib/a.so extra\n",
            "wrong lib/a.so\n",
            "name=adbd lib/a.so\nname=adbd lib/missing.so\n",
            "name=adbd lib/a.so\n# trailing\nname=adbd lib/b.so\n",
        ):
            with self.subTest(manifest=manifest):
                shutil.rmtree(self.root)
                self.root.mkdir()
                self.module("alpha", manifest)
                self.assertEqual(self.collect(), ["count=0"])

    def test_rejects_library_outside_the_module(self):
        other = self.module("other", None)
        (other / "away.so").write_bytes(self.library)
        self.module("alpha", "name=adbd lib/a.so\n")
        link = self.root / "alpha/lib/a.so"
        link.unlink()
        link.symlink_to(other / "away.so")
        self.assertEqual(self.collect(), ["count=0"])

    def test_accepts_symlink_inside_the_module(self):
        self.module("alpha", "name=adbd lib/a.so\n")
        library = self.root / "alpha/lib/a.so"
        target = self.root / "alpha/lib/real.so"
        library.rename(target)
        library.symlink_to(target)
        self.assertEqual(self.collect()[1], f"module={self.root}/alpha/lib/a.so companion=0")

    def test_requires_matching_elf(self):
        self.module("alpha", "name=adbd lib/a.so\n")
        (self.root / "alpha/lib/a.so").write_bytes(b"not an elf")
        self.assertEqual(self.collect(), ["count=0"])

    def test_rejects_wrong_machine(self):
        self.module("alpha", "name=adbd lib/a.so\n")
        other = next(elf for elf in ELF_BYTES.values() if elf != self.library)
        (self.root / "alpha/lib/a.so").write_bytes(other)
        self.assertEqual(self.collect(), ["count=0"])

    def test_invalid_module_does_not_discard_other_modules(self):
        for name in ("a", "b", "c"):
            self.module(name, "# a comment with several words\nname=adbd lib/a.so\n")
        order = list(self.root.iterdir())
        with (order[-1] / "zn_modules.txt").open("a") as manifest:
            manifest.write("invalid\n")
        self.assertEqual(self.collect()[0], "count=2")

    def test_deduplicates_repeated_declarations(self):
        self.module("alpha", "name=adbd lib/a.so\nname=adbd companion lib/a.so\n")
        self.assertEqual(self.collect()[-1].endswith("companion=1"), True)
        self.assertEqual(len(self.collect()), 2)


if __name__ == "__main__":
    unittest.main()
