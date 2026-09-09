#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GNU GPL v3; see LICENSE.
"""Compiler/platform contracts, checked without executing a cross compiler."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("core_check", ROOT / "check.py")
CHECK = importlib.util.module_from_spec(spec)
spec.loader.exec_module(CHECK)


class MakeContracts(unittest.TestCase):
    def test_switching_platform_restores_its_library_without_recompiling(self):
        # Exercise the actual build rules with a small native C library. No
        # cross compiler is needed to reproduce the shared-output regression.
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for name in ("Makefile", "Makefile.common", "src/libretro/libretro_sources.mk",
                         "src/libretro/libretro.exports", "src/libretro/libretro.exports.macho"):
                destination = root / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / name, destination)
            (root / "probe.c").write_text(
                '__attribute__((visibility("default"))) '
                'int retro_api_version(void) { return TEST_VALUE; }\n')
            (root / "src/libretro/libretro.exports.macho").write_text("_retro_api_version\n")
            env = {k: v for k, v in os.environ.items() if k not in
                   {"MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "MAKEFILES", "CC",
                    "CFLAGS", "CPPFLAGS", "LDFLAGS", "LDLIBS"}}
            binaries = []
            logs = []
            for target, value in (("a", 1), ("b", 2), ("a", 1)):
                platform = ("osx-" if CHECK.sys.platform == "darwin" else "unix-") + target
                cp = subprocess.run(["make", "all", "platform=" + platform, "CC=cc",
                    "SOURCES_C=probe.c", "CFLAGS=-O2 -DTEST_VALUE=" + str(value),
                    "CPPFLAGS=", "LDFLAGS=", "LDLIBS="],
                    cwd=root, env=env, text=True, capture_output=True)
                self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
                extension = ".dylib" if CHECK.sys.platform == "darwin" else ".so"
                binaries.append((root / ("zesarpcw_libretro" + extension)).read_bytes())
                logs.append(cp.stdout)
            self.assertNotEqual(binaries[0], binaries[1])
            self.assertEqual(binaries[0], binaries[2])
            self.assertNotIn(" -c ", logs[2])

    def commands(self, platform="unix", args=(), environment=None, success=True):
        env = os.environ.copy()
        for key in ("CC", "CFLAGS", "CPPFLAGS", "LDFLAGS", "LDLIBS", "MAKEFLAGS", "MFLAGS",
                    "GNUMAKEFLAGS", "MAKEFILES", "CROSS_COMPILE", "LIBRETRO_APPLE_PLATFORM",
                    "LIBRETRO_APPLE_ISYSROOT", "MACOSX_DEPLOYMENT_TARGET"):
            env.pop(key, None)
        env.update(environment or {})
        cp = subprocess.run(["make", "-nB", "all", "platform=" + platform, *args],
                            cwd=ROOT, env=env, text=True, capture_output=True)
        if not success:
            self.assertNotEqual(cp.returncode, 0)
            return cp.stderr
        self.assertEqual(cp.returncode, 0, cp.stderr)
        commands = [shlex.split(line) for line in cp.stdout.splitlines() if line and not line.startswith("make[")]
        commands = [c for c in commands if "-c" in c or "-shared" in c or "-dynamiclib" in c]
        self.assertTrue(commands)
        return commands

    def test_defaults_including_no_builtin_variables(self):
        for platform, compiler in (("unix", "gcc"), ("win", "x86_64-w64-mingw32-gcc"),
                                   ("win64", "x86_64-w64-mingw32-gcc"), ("osx", "clang")):
            for args in ((), ("-rR",)):
                with self.subTest(platform=platform, args=args):
                    for cmd in self.commands(platform, args):
                        self.assertEqual(cmd[0], compiler)

    def test_environment_and_command_line_compilers(self):
        for platform in ("unix", "win", "win64", "osx-arm64", "android"):
            env = {"CC": "x86_64-w64-mingw32.static-gcc"}
            with self.subTest(platform=platform):
                for cmd in self.commands(platform, environment=env):
                    self.assertEqual(cmd[0], env["CC"])
                for cmd in self.commands(platform, ("CC=chosen-compiler",), env):
                    self.assertEqual(cmd[0], "chosen-compiler")

    def test_apple_target_and_sdk_reach_compile_and_link(self):
        env = {"CC": "apple-clang", "CROSS_COMPILE": "1",
               "LIBRETRO_APPLE_PLATFORM": "arm64-apple-macos10.15",
               "LIBRETRO_APPLE_ISYSROOT": "/SDK With Spaces/MacOSX.sdk"}
        commands = self.commands("osx", ("CFLAGS=-O1 -DUSER_C", "CPPFLAGS=-DUSER_CPP",
                                        "LDFLAGS=-Wl,-user-link", "LDLIBS=-luser"), env)
        for cmd in commands:
            self.assertEqual(cmd[cmd.index("-target")+1], env["LIBRETRO_APPLE_PLATFORM"])
            self.assertEqual(cmd[cmd.index("-isysroot")+1], env["LIBRETRO_APPLE_ISYSROOT"])
            if "-c" in cmd:
                self.assertIn("-DUSER_C", cmd)
                self.assertIn("-DUSER_CPP", cmd)
            else:
                self.assertIn("-Wl,-user-link", cmd)
                self.assertIn("-luser", cmd)

    def test_incomplete_apple_cross_configuration_fails(self):
        self.assertIn("requires LIBRETRO_APPLE_PLATFORM", self.commands(
            "osx", environment={"CROSS_COMPILE": "1"}, success=False))
        self.assertIn("requires LIBRETRO_APPLE_ISYSROOT", self.commands(
            "osx", environment={"CROSS_COMPILE": "1", "LIBRETRO_APPLE_PLATFORM": "arm64-apple-macos11"}, success=False))

    def test_native_apple_architecture_is_explicit(self):
        for arch in ("x86_64", "arm64"):
            for cmd in self.commands("osx-" + arch):
                self.assertEqual(cmd[cmd.index("-arch")+1], arch)

    def test_android_uses_elf_export_policy(self):
        commands = self.commands("android", ("CC=ndk-clang",))
        link = next(c for c in commands if "-shared" in c)
        self.assertTrue(any("--version-script=src/libretro/libretro.exports" in flag for flag in link))
        self.assertTrue(any("max-page-size=16384" in flag for flag in link))
        self.assertTrue(any("common-page-size=16384" in flag for flag in link))


class CheckerContracts(unittest.TestCase):
    def test_optional_database_preserves_required_metadata_checks(self):
        metadata = (ROOT / "zesarpcw_libretro.info").read_text()
        # A future database contribution may add this standard libretro field;
        # its presence must not prevent contributors from running make check.
        metadata += '\ndatabase = "Amstrad - PCW"\n'
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            info = root / "zesarpcw_libretro.info"
            info.write_text(metadata)
            with patch.object(CHECK, "ROOT", root):
                CHECK.check_metadata()
                for invalid, message in (
                    (metadata.replace('supported_extensions = "dsk|m3u"',
                                      'supported_extensions = "rom"'), "supported_extensions"),
                    (metadata + 'need_fullpath = "true"\n', "needs_fullpath"),
                ):
                    with self.subTest(message=message):
                        info.write_text(invalid)
                        with self.assertRaisesRegex(SystemExit, message):
                            CHECK.check_metadata()

    def test_android_requires_arm64_and_16k_load_segments(self):
        def elf(align=16384, address=0, machine=183, kind=3, count=1):
            ident = b"\x7fELF\x02\x01\x01" + b"\0" * 9
            header = ident + struct.pack("<HHIQQQIHHHHHH", kind,
                machine, 1, 0, 64, 0, 0, 64, 56, count, 0, 0, 0)
            return header + struct.pack("<II6Q", 1, 5, 0, address, 0, 120, 120, align)

        with tempfile.TemporaryDirectory() as td:
            core = Path(td) / "probe.so"
            for align in (16384, 65536):
                core.write_bytes(elf(align=align))
                with contextlib.redirect_stdout(io.StringIO()):
                    CHECK.check_android_target(core)
            bad = [elf(align=4096), elf(align=24576), elf(address=4096),
                   elf(machine=62), elf(kind=2), elf(count=0), elf(count=2),
                   elf()[:70], b"not an ELF library"]
            for data in bad:
                core.write_bytes(data)
                with self.subTest(data=data[:64]), self.assertRaises(SystemExit):
                    CHECK.check_android_target(core)

    def test_runtime_runs_only_for_a_matching_host(self):
        cases = (("darwin", "arm64", "osx-arm64", True),
                 ("darwin", "arm64", "osx-x86_64", False),
                 ("darwin", "x86_64", "osx-x86_64", True),
                 ("linux", "x86_64", "osx-x86_64", False),
                 ("linux", "aarch64", "linux-aarch64", True),
                 ("linux", "x86_64", "linux-aarch64", False),
                 ("linux", "x86_64", "unix", True),
                 ("linux", "x86_64", "win64", False),
                 ("linux", "aarch64", "android-arm64", False))
        for host, cpu, target, want in cases:
            with self.subTest(host=host, cpu=cpu, target=target), patch.object(CHECK.sys, "platform", host), \
                    patch.object(CHECK.host_platform, "machine", return_value=cpu), patch.dict(os.environ, {}, clear=True):
                self.assertEqual(CHECK.native_runtime(target), want)
                self.assertEqual(CHECK.dead_strip_flag(), "-Wl,-dead_strip" if host == "darwin" else "-Wl,--gc-sections")

    def test_macho_architecture_and_deployment_target(self):
        with tempfile.TemporaryDirectory() as td:
            core = Path(td) / "probe.dylib"
            for cpu, arch, minimum in ((0x01000007, "x86_64", 0x000a0900),
                                       (0x0100000c, "arm64", 0x000b0000)):
                core.write_bytes(struct.pack("<8I", 0xfeedfacf, cpu, 0, 6, 1, 24, 0, 0)
                                 + struct.pack("<6I", 0x32, 24, 1, minimum, 0, 0))
                env = {"MACOSX_DEPLOYMENT_TARGET": "10.9"} if arch == "x86_64" else {
                    "CROSS_COMPILE": "1", "LIBRETRO_APPLE_PLATFORM": "arm64-apple-macos10.15"}
                with patch.dict(os.environ, env, clear=True), contextlib.redirect_stdout(io.StringIO()):
                    CHECK.check_macho_target(core, "osx-" + arch)
                with patch.dict(os.environ, {}, clear=True), self.assertRaises(SystemExit):
                    CHECK.check_macho_target(core, "osx-" + ("arm64" if arch == "x86_64" else "x86_64"))
                with patch.dict(os.environ, {"MACOSX_DEPLOYMENT_TARGET": "12.0"}, clear=True), self.assertRaises(SystemExit):
                    CHECK.check_macho_target(core, "osx-" + arch)

    def test_pe_checker_rejects_exports_outside_libretro(self):
        # Both GNU objdump table styles used by supported MinGW toolchains.
        for hints in (False, True):
            table = "[Ordinal/Name Pointer] Table\n" + "\n".join(
                f"[ {i}] " + (f"+base[ {i+1}]  {i:04x} " if hints else "") + name
                for i, name in enumerate(sorted(CHECK.EXPECTED_EXPORTS))) + "\n\n"
            with patch.object(CHECK.shutil, "which", return_value="objdump"), \
                    patch.object(CHECK, "run", return_value=subprocess.CompletedProcess([], 0, table, "")):
                CHECK.check_exports(Path("probe.dll"), "win64")
            bad = table.rstrip() + "\n[ 25] accidental_helper\n\n"
            with patch.object(CHECK.shutil, "which", return_value="objdump"), \
                    patch.object(CHECK, "run", return_value=subprocess.CompletedProcess([], 0, bad, "")), \
                    self.assertRaises(SystemExit):
                CHECK.check_exports(Path("probe.dll"), "win64")


if __name__ == "__main__":
    unittest.main()
