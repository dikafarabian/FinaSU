"""On-device check of the ZN host against a mock daemon.

Requires a device selected with ANDROID_SERIAL. Everything runs from /data/local/tmp and is
removed afterwards; no engine, module directory, or system process is touched.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
REMOTE = "/data/local/tmp/zn-host-test"
NDK = Path(os.environ.get("ANDROID_HOME", Path.home() / "Android/Sdk")) / "ndk/29.0.14206865"
CLANG = NDK / "toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android25-clang"

CSOLOADER_INCLUDE = ROOT / "loader/src/external/csoloader/include"
PLTI_INCLUDE = ROOT / "loader/src/external/plti/src"
LOADER_INCLUDES = [
    ROOT / "loader/src/include",
    ROOT / "loader/src/injector",
    ROOT / "include",
    CSOLOADER_INCLUDE,
    PLTI_INCLUDE,
]
LOADER_SOURCES = [
    ROOT / "loader/src/injector/zn_api.c",
    ROOT / "loader/src/common/daemon.c",
    ROOT / "loader/src/common/socket_utils.c",
    ROOT / "loader/src/common/misc.c",
]
CSOLOADER_SOURCES = [
    ROOT / "loader/src/external/csoloader/src/csoloader.c",
    ROOT / "loader/src/external/csoloader/src/elf_util.c",
    ROOT / "loader/src/external/csoloader/src/linker.c",
    ROOT / "loader/src/external/csoloader/src/carray.c",
    ROOT / "loader/src/external/csoloader/src/sleb128.c",
    ROOT / "loader/src/external/csoloader/src/backtrace-support.c",
]
PLTI_SOURCES = [
    ROOT / "loader/src/external/plti/src/plti.c",
    ROOT / "loader/src/external/plti/src/elf_util.c",
    ROOT / "loader/src/external/plti/src/fd_utils.c",
]


def adb(*args, **kwargs):
    serial = os.environ.get("ANDROID_SERIAL", "")
    prefix = ["-s", serial] if serial else []
    return subprocess.run(["adb", *prefix, *args], capture_output=True, text=True, **kwargs)


def device_available():
    serial = os.environ.get("ANDROID_SERIAL")
    if not serial or not CLANG.exists() or not shutil.which("adb"):
        return False
    devices = subprocess.run(["adb", "devices"], capture_output=True, text=True).stdout.splitlines()[1:]
    attached = [line.split()[0] for line in devices if line.split()[1:2] == ["device"]]
    return serial in attached


@unittest.skipUnless(device_available(), "no adb device or NDK")
class ZnDeviceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        build = Path(cls.directory.name)
        base_flags = ["-std=gnu18", "-D_GNU_SOURCE", "-O1", "-w",
                      f'-DFOLKZYGISK_RUNTIME_DIR="{REMOTE}"']

        def compile_source(source, includes, tag="loader"):
            obj = build / f"{tag}-{source.stem}.o"
            subprocess.run([str(CLANG), *base_flags,
                            *[f"-I{path}" for path in includes], "-c", str(source), "-o", str(obj)],
                           check=True)
            return obj

        objects = [compile_source(source, LOADER_INCLUDES) for source in LOADER_SOURCES]
        objects += [compile_source(source, [CSOLOADER_INCLUDE], "csoloader") for source in CSOLOADER_SOURCES]
        objects += [compile_source(source, [PLTI_INCLUDE], "plti") for source in PLTI_SOURCES]
        objects.append(compile_source(ROOT / "tests/zn_host_smoke.c", LOADER_INCLUDES))

        cls.smoke = build / "smoke"
        subprocess.run([str(CLANG), *map(str, objects), "-llog", "-ldl", "-o", str(cls.smoke)], check=True)

        cls.mock = build / "mock-daemon"
        subprocess.run([str(CLANG), *base_flags, f'-DZN_TEST_RUNTIME="{REMOTE}"',
                        str(ROOT / "tests/zn_mock_daemon.c"), "-o", str(cls.mock)], check=True)
        cls.module = build / "module.so"
        subprocess.run([str(CLANG), *base_flags, *[f"-I{path}" for path in LOADER_INCLUDES],
                        "-shared", "-fPIC", str(ROOT / "tests/zn_host_fixture.c"),
                        "-o", str(cls.module)], check=True)

        adb("shell", "rm", "-rf", REMOTE)
        adb("shell", "mkdir", "-p", REMOTE)
        for path in (cls.smoke, cls.mock, cls.module):
            result = adb("push", str(path), f"{REMOTE}/{path.name}")
            if result.returncode != 0:
                raise AssertionError(result.stderr)
        adb("shell", "chmod", "700", f"{REMOTE}/smoke", f"{REMOTE}/mock-daemon")

    @classmethod
    def tearDownClass(cls):
        adb("shell", "pkill", "-f", f"{REMOTE}/mock-daemon")
        adb("shell", "rm", "-rf", REMOTE)

    def run_case(self, companion):
        token = uuid.uuid4().hex[:8]
        output = f"{REMOTE}/out-{token}"
        adb("shell", "pkill", "-f", f"{REMOTE}/mock-daemon")
        adb("shell", f"{REMOTE}/mock-daemon {REMOTE}/module.so {companion} > {REMOTE}/log-{token} 2>&1 &")
        for _ in range(50):
            if "ready" in adb("shell", "cat", f"{REMOTE}/log-{token}").stdout.splitlines():
                break
            subprocess.run(["sleep", "0.1"])
        else:
            self.fail("mock daemon did not become ready")
        result = adb("shell", f"ZN_TEST_OUT={output} {REMOTE}/smoke")
        adb("shell", "pkill", "-f", f"{REMOTE}/mock-daemon")
        cat = adb("shell", "cat", output)
        adb("shell", "rm", "-f", output)
        return result, cat.stdout.split()

    def test_module_receives_api_without_runtime(self):
        result, lines = self.run_case(0)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("smoke-done", result.stdout)
        self.assertIn("loaded=1", lines)
        self.assertIn("api=1", lines)
        self.assertIn("runtime=0", lines)
        self.assertIn("resolver=1", lines)
        self.assertIn("getpid=1", lines)
        self.assertIn("prefix=1", lines)
        self.assertIn("inline=1", lines)

    def test_companion_roundtrip(self):
        _, lines = self.run_case(1)
        self.assertIn("companion_fd=1", lines)
        self.assertIn("companion_io=1", lines)


if __name__ == "__main__":
    unittest.main()
