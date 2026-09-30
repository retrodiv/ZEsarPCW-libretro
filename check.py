#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>
# Distributed under the GNU General Public License, version 3, WITHOUT ANY
# WARRANTY. See the LICENSE file at the repository root for the full terms.
"""Build, libretro API and runtime checks for ZEsarPCW contributors."""

from __future__ import annotations

import argparse
import json
import os
import platform as host_platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
EXPECTED_EXPORTS = {
    "retro_api_version", "retro_cheat_reset", "retro_cheat_set", "retro_deinit",
    "retro_get_memory_data", "retro_get_memory_size", "retro_get_region",
    "retro_get_system_av_info", "retro_get_system_info", "retro_init",
    "retro_load_game", "retro_load_game_special", "retro_reset", "retro_run",
    "retro_serialize", "retro_serialize_size", "retro_set_audio_sample",
    "retro_set_audio_sample_batch", "retro_set_controller_port_device",
    "retro_set_environment", "retro_set_input_poll", "retro_set_input_state",
    "retro_set_video_refresh", "retro_unload_game", "retro_unserialize",
}


def fail(message: str) -> None:
    raise SystemExit(f"CHECK FAILED: {message}")


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, text=True, check=True, **kwargs)


def locate_test(relative: str) -> Path:
    local = ROOT / "tests" / relative
    if local.is_file():
        return local
    fail(f"test source missing: tests/{relative}")
    raise AssertionError


def check_metadata() -> None:
    info_path = ROOT / "zesarpcw_libretro.info"
    if not info_path.is_file():
        fail("zesarpcw_libretro.info is missing")
    values: dict[str, str] = {}
    for raw in info_path.read_text(encoding="utf-8").splitlines():
        if "=" in raw:
            key, value = raw.split("=", 1)
            values[key.strip()] = value.strip().strip('"')
    expected = {
        "display_version": "Git",
        "supported_extensions": "dsk|m3u", "needs_fullpath": "true",
        "supports_no_game": "false", "disk_control": "true",
        "savestate_features": "serialized", "cheats": "true",
        "input_descriptors": "true", "core_options": "true",
        "core_options_version": "2.0", "hw_render": "false",
    }
    for key, want in expected.items():
        if values.get(key) != want:
            fail(f"metadata {key!r}: got {values.get(key)!r}, expected {want!r}")
    if "need_fullpath" in values:
        fail("metadata uses need_fullpath instead of needs_fullpath")
    run([sys.executable, str(ROOT / "tools/version.py"), "check"])


def check_export_policy() -> None:
    gnu_path = ROOT / "src" / "libretro" / "libretro.exports"
    macho_path = ROOT / "src" / "libretro" / "libretro.exports.macho"
    if not gnu_path.is_file() or not macho_path.is_file():
        fail("platform export policy file is missing")
    gnu = set(re.findall(r"^\s*(retro_[A-Za-z0-9_]+);", gnu_path.read_text(), re.M))
    macho = {line.strip().removeprefix("_")
             for line in macho_path.read_text().splitlines()
             if line.strip() and not line.lstrip().startswith("#")}
    if gnu != EXPECTED_EXPORTS:
        fail(f"ELF export policy differs: extra={sorted(gnu-EXPECTED_EXPORTS)}, "
             f"missing={sorted(EXPECTED_EXPORTS-gnu)}")
    if macho != EXPECTED_EXPORTS:
        fail(f"Mach-O export policy differs: extra={sorted(macho-EXPECTED_EXPORTS)}, "
             f"missing={sorted(EXPECTED_EXPORTS-macho)}")


def check_exports(core: Path, platform: str) -> None:
    if "android" in platform:
        check_android_target(core, platform)
    if "win" in platform:
        check_pe_target(core, platform)
        prefix = "i686" if platform in ("win32", "windows-x86", "win-i686") else "x86_64"
        objdump = shutil.which(prefix + "-w64-mingw32-objdump") or shutil.which("objdump")
        if not objdump:
            fail("objdump is required to validate Windows exports")
        cp = run([objdump, "-p", str(core)], capture_output=True)
        exports = set()
        in_names = False
        for line in cp.stdout.splitlines():
            if "[Ordinal/Name Pointer] Table" in line:
                in_names = True
                continue
            if in_names:
                match = re.match(r"\s*\[\s*\d+\].*\s(\S+)\s*$", line)
                if match:
                    exports.add(match.group(1))
                elif exports and not line.strip():
                    break
        if exports != EXPECTED_EXPORTS:
            fail(f"PE exports differ: extra={sorted(exports-EXPECTED_EXPORTS)}, "
                 f"missing={sorted(EXPECTED_EXPORTS-exports)}")
        return
    if "osx" in platform:
        check_macho_target(core, platform)
        nm = shutil.which("llvm-nm") or "nm"
        cp = run([nm, "-gU", str(core)], capture_output=True)
        exports = set()
        for line in cp.stdout.splitlines():
            match = re.search(r"\s(_[A-Za-z0-9_]+)$", line)
            if match:
                exports.add(match.group(1).removeprefix("_"))
        if exports != EXPECTED_EXPORTS:
            fail(f"Mach-O exports differ: extra={sorted(exports-EXPECTED_EXPORTS)}, "
                 f"missing={sorted(EXPECTED_EXPORTS-exports)}")
        return
    if platform.startswith("linux-"):
        check_linux_target(core, platform)
    cp = run(["nm", "-D", "--defined-only", str(core)], capture_output=True)
    exports = {line.split()[-1] for line in cp.stdout.splitlines() if line.split()}
    if exports != EXPECTED_EXPORTS:
        fail(f"dynamic exports differ: extra={sorted(exports-EXPECTED_EXPORTS)}, "
             f"missing={sorted(EXPECTED_EXPORTS-exports)}")


def check_linux_target(core: Path, platform: str) -> None:
    expected = {"linux-x86": (1, 3), "linux-i686": (1, 3),
                "linux-x86_64": (2, 62), "linux-aarch64": (2, 183),
                "linux-armv7": (1, 40), "linux-armhf": (1, 40)}.get(platform)
    if expected is None:
        return
    data = core.read_bytes()
    elf_class, machine = expected
    header_size = 52 if elf_class == 1 else 64
    if (len(data) < header_size or data[:4] != b"\x7fELF"
            or data[4:7] != bytes((elf_class, 1, 1))
            or struct.unpack_from("<HH", data, 16) != (3, machine)):
        fail(f"expected a {platform} ELF shared library")
    if machine == 40:
        flags = struct.unpack_from("<I", data, 36)[0]
        if flags & 0xff000000 != 0x05000000 or flags & 0x600 != 0x400:
            fail("expected ARM EABI5 with the hard-float ABI (armhf)")


def check_pe_target(core: Path, platform: str) -> None:
    expected = {"win32": (0x14c, 0x10b), "windows-x86": (0x14c, 0x10b),
                "win-i686": (0x14c, 0x10b), "win64": (0x8664, 0x20b),
                "windows-x86_64": (0x8664, 0x20b)}.get(platform)
    if expected is None:
        return
    data = core.read_bytes()
    if len(data) < 64 or data[:2] != b"MZ":
        fail("expected a Windows PE library")
    offset = struct.unpack_from("<I", data, 60)[0]
    if offset < 64 or offset + 26 > len(data) or data[offset:offset+4] != b"PE\0\0":
        fail("invalid Windows PE header")
    machine = struct.unpack_from("<H", data, offset + 4)[0]
    flags, magic = struct.unpack_from("<HH", data, offset + 22)
    if (machine, magic) != expected or not flags & 0x2000:
        fail(f"expected a {platform} PE DLL")


def check_android_target(core: Path, platform: str = "android-arm64") -> None:
    """Check the requested Android ARM architecture and 16 KiB load alignment."""
    data = core.read_bytes()
    armv7 = platform == "android-armv7"
    elf_class, machine, header_size = (1, 40, 52) if armv7 else (2, 183, 64)
    if len(data) < header_size or data[:7] != b"\x7fELF" + bytes((elf_class, 1, 1)):
        fail(f"expected an {platform} little-endian ELF library")
    kind, machine = struct.unpack_from("<HH", data, 16)
    if kind != 3 or machine != (40 if armv7 else 183):
        fail(f"expected an {platform} shared library")
    offset = struct.unpack_from("<I" if armv7 else "<Q", data, 28 if armv7 else 32)[0]
    size, count = struct.unpack_from("<HH", data, 42 if armv7 else 54)
    if size != (32 if armv7 else 56) or not count or offset < header_size or offset + count * size > len(data):
        fail("invalid Android ELF program headers")
    loads = 0
    for index in range(count):
        if armv7:
            kind, file_offset, address, _, file_size, memory_size, _, alignment = struct.unpack_from(
                "<8I", data, offset + index * size)
        else:
            kind, _, file_offset, address, _, file_size, memory_size, alignment = struct.unpack_from(
                "<II6Q", data, offset + index * size)
        if kind != 1:  # PT_LOAD
            continue
        loads += 1
        if file_size > memory_size or file_offset + file_size > len(data):
            fail("invalid Android ELF load segment")
        if (alignment < 16384 or alignment & (alignment - 1)
                or (address - file_offset) % alignment):
            fail("Android ELF load segments require 16 KiB page alignment")
    if not loads:
        fail("Android ELF has no load segments")
    print(f"PASS: {platform} ELF, load segments aligned for 16 KiB pages")


def macho_target(core: Path) -> tuple[str, tuple[int, int, int]]:
    """Read the architecture and macOS minimum from a thin 64-bit Mach-O dylib."""
    data = core.read_bytes()
    if len(data) < 32:
        fail("truncated Mach-O header")
    magic, cpu, _, kind, count, command_bytes, _, _ = struct.unpack_from("<8I", data)
    architectures = {0x01000007: "x86_64", 0x0100000c: "arm64"}
    if magic != 0xfeedfacf or kind != 6 or cpu not in architectures:
        fail("expected a thin x86_64/arm64 Mach-O dylib")
    end = 32 + command_bytes
    if end > len(data):
        fail("truncated Mach-O load commands")
    offset, minimum = 32, None
    for _ in range(count):
        if end - offset < 8:
            fail("truncated Mach-O load command")
        command, size = struct.unpack_from("<2I", data, offset)
        if size < 8 or size > end - offset:
            fail("invalid Mach-O load command size")
        version = None
        if command == 0x24 and size >= 16:  # LC_VERSION_MIN_MACOSX
            version = struct.unpack_from("<I", data, offset + 8)[0]
        if command == 0x32 and size >= 24:  # LC_BUILD_VERSION
            target_os, version = struct.unpack_from("<2I", data, offset + 8)
            if target_os != 1:
                fail("Mach-O target platform is not macOS")
        if version is not None:
            minimum = (version >> 16, (version >> 8) & 255, version & 255)
        offset += size
    if offset != end or minimum is None:
        fail("Mach-O macOS deployment target is missing or malformed")
    return architectures[cpu], minimum


def check_macho_target(core: Path, platform: str) -> None:
    architecture, minimum = macho_target(core)
    triple = os.environ.get("LIBRETRO_APPLE_PLATFORM", "") if os.environ.get("CROSS_COMPILE") == "1" else ""
    expected = triple.split("-")[0] if triple else platform.removeprefix("osx-")
    if expected in ("x86_64", "arm64") and architecture != expected:
        fail(f"Mach-O architecture {architecture}, expected {expected}")
    match = re.search(r"macos(?:x)?([0-9.]+)", triple)
    requested = match.group(1) if match else os.environ.get("MACOSX_DEPLOYMENT_TARGET", "")
    if requested:
        parts = tuple(int(v) for v in requested.split("."))
        want = (parts + (0, 0, 0))[:3]
        # Apple Silicon macOS starts at 11.0; Clang promotes earlier requests.
        if architecture == "arm64":
            want = max(want, (11, 0, 0))
        if minimum != want:
            fail(f"Mach-O minimum macOS {minimum}, expected {want}")
    print(f"PASS: Mach-O {architecture}, minimum macOS {'.'.join(map(str, minimum))}")


def native_runtime(platform: str) -> bool:
    machine = {"aarch64": "arm64", "amd64": "x86_64"}.get(
        host_platform.machine().lower(), host_platform.machine().lower())
    if "win" in platform or "android" in platform:
        return False
    if "osx" in platform:
        if sys.platform != "darwin":
            return False
        triple = os.environ.get("LIBRETRO_APPLE_PLATFORM", "") if os.environ.get("CROSS_COMPILE") == "1" else ""
        expected = triple.split("-")[0] if triple else platform.removeprefix("osx-")
        return expected == "osx" or expected == machine
    if "aarch64" in platform or "arm64" in platform:
        return sys.platform.startswith("linux") and machine == "arm64"
    if platform in ("linux-armv7", "linux-armhf"):
        return sys.platform.startswith("linux") and machine.startswith("armv7") and struct.calcsize("P") == 4
    if platform in ("linux-x86", "linux-i686", "unix-x86"):
        return (sys.platform.startswith("linux") and struct.calcsize("P") == 4
                and machine in ("i386", "i486", "i586", "i686", "x86", "x86_64"))
    return sys.platform.startswith("linux") and ("x86_64" not in platform or machine == "x86_64")


def dead_strip_flag() -> str:
    return "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"


def compiler_profile(sanitizer: str) -> list[str]:
    if sanitizer == "address":
        return ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    if sanitizer == "thread":
        return ["-O1", "-g", "-fsanitize=thread", "-fno-omit-frame-pointer"]
    return ["-O2"]


def check_crop_unit(cc: str, temp: Path, sanitizer: str) -> None:
    test = locate_test("test_crop.c")
    exe = temp / "test_crop"
    flags = compiler_profile(sanitizer)
    run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
         "-I", str(ROOT / "src" / "libretro"), str(test),
         str(ROOT / "src" / "libretro" / "pcw_crop.c"), "-o", str(exe)])
    run([str(exe)])


def check_audio_unit(cc: str, temp: Path, sanitizer: str) -> None:
    test = locate_test("test_audio.c")
    exe = temp / "test_audio"
    flags = compiler_profile(sanitizer)
    includes = ["src", "src/libretro", "src/audio", "src/machines",
                "src/storage", "src/soundchips"]
    for char_flag in ("-fsigned-char", "-funsigned-char"):
        run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags, char_flag,
             "-ffunction-sections", "-fdata-sections",
             *(flag for path in includes for flag in ("-I", str(ROOT / path))),
             str(test), *(str(ROOT / "src" / path) for path in
                          ("libretro/audiolibretro.c", "libretro/pcw_audio.c",
                           "soundchips/ay38912.c", "libretro/pcw_fdc_sound.c")),
             dead_strip_flag(), "-o", str(exe)])
        run([str(exe)])


def check_keyboard_unit(cc: str, temp: Path, sanitizer: str) -> None:
    test = locate_test("test_keyboard.c")
    exe = temp / "test_keyboard"
    flags = compiler_profile(sanitizer)
    includes = ["src", "src/libretro", "src/audio", "src/cores", "src/cpus",
                "src/machines", "src/storage", "src/soundchips"]
    run([cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra",
         "-Werror", *flags,
         *(flag for path in includes for flag in ("-I", str(ROOT / path))),
         str(test), str(ROOT / "src" / "libretro" / "pcw_keyboard.c"),
         "-o", str(exe)])
    run([str(exe)])


def check_osk_unit(cc: str, temp: Path, sanitizer: str) -> None:
    test = locate_test("test_osk.c")
    exe = temp / "test_osk"
    flags = compiler_profile(sanitizer)
    includes = ["src", "src/libretro", "src/audio", "src/cores", "src/cpus",
                "src/machines", "src/storage", "src/soundchips"]
    run([cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra",
         "-Werror", *flags,
         *(flag for path in includes for flag in ("-I", str(ROOT / path))),
         str(test), str(ROOT / "src" / "libretro" / "pcw_osk.c"),
         str(ROOT / "src" / "libretro" / "pcw_keyboard.c"),
         "-o", str(exe)])
    run([str(exe)])


def check_state_rle_unit(cc: str, temp: Path, sanitizer: str) -> None:
    test = locate_test("test_state_rle.c")
    exe = temp / "test_state_rle"
    flags = compiler_profile(sanitizer)
    run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
         "-I", str(ROOT / "src" / "libretro"), str(test),
         str(ROOT / "src" / "libretro" / "pcw_state_rle.c"), "-o", str(exe)])
    run([str(exe)])


def check_state_capacity_unit(cc: str, temp: Path, sanitizer: str) -> None:
    test = locate_test("test_state_capacity.c")
    exe = temp / "test_state_capacity"
    includes = ["src", "src/libretro", "src/audio", "src/cores", "src/machines",
                "src/soundchips"]
    run([cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-DCOMPILE_LIBRETRO",
         "-Wall", "-Wextra", "-Werror", *compiler_profile(sanitizer),
         "-ffunction-sections", "-fdata-sections",
         *(flag for path in includes for flag in ("-I", str(ROOT / path))),
         str(test), str(ROOT / "src/libretro/pcw_state_rle.c"), dead_strip_flag(),
         "-o", str(exe)])
    run([str(exe)])


def check_core_failures(cc: str, temp: Path, sanitizer: str) -> None:
    # GNU ld wraps only the test executable's real engine objects. Public core
    # binaries keep their normal allocators, entry points and export surface.
    exe = temp / "test_core_failures"
    flags = compiler_profile(sanitizer)
    run(["make", "-j2", "-C", str(ROOT), "-f", "Makefile", "-f", "tests/core_failures.mk",
         "core-failures", "platform=unix-failures-" + (sanitizer or "normal"),
         "CC=" + cc, "CFLAGS=" + " ".join(flags),
         "LDFLAGS=" + " ".join(flags), "FAILURE_TEST_OUTPUT=" + str(exe)])
    missing = temp / "missing.dsk"
    invalid = temp / "invalid.m3u"
    invalid.write_text(str(missing) + "\n")
    partial = temp / "partial.m3u"
    disk = ROOT / "src/openpcw_os.dsk"
    partial.write_text(str(missing) + "\n" + str(disk) + "\n")
    result = run([str(exe), str(disk), str(missing), str(invalid), str(partial),
                  str(temp / "short.dsk")],
                 capture_output=True)
    output = result.stdout + result.stderr
    for marker in ("PASS: failures, retry and logging (frontend)",
                   "PASS: failures, retry and logging (stderr)"):
        if marker not in output:
            print(output, file=sys.stderr)
            fail("fault/logging host did not complete: " + marker)
    print("PASS: allocation/init/frame/reset failures, shutdown refusal, retry and both log paths")
    print("PASS: empty/truncated disks rejected on reload and tray insertion; valid retry succeeds")
    disk_exe = temp / "test_disk_state"
    run(["make", "-j2", "-C", str(ROOT), "-f", "Makefile", "-f", "tests/core_failures.mk",
         "disk-state", "platform=unix-failures-" + (sanitizer or "normal"),
         "CC=" + cc, "CFLAGS=" + " ".join(flags), "LDFLAGS=" + " ".join(flags),
         "DISK_STATE_TEST_OUTPUT=" + str(disk_exe)])
    run([str(disk_exe), str(ROOT / "src/openpcw_os.dsk")])



def check_loading_states(core: Path, host: Path, temp: Path) -> None:
    # Compare continued execution, not just a serialization round-trip. Use only
    # the redistributable bundled disk; no private corpus or firmware needed.
    disk = temp / "boot.dsk"
    disk.write_bytes((ROOT / "src/openpcw_os.dsk").read_bytes())
    for model in ("PCW8256", "PCW8512"):
        results = []
        for label, frames, save, restore in (("plain", 600, -1, -1),
                                              ("restored", 900, 100, 400)):
            hashes = temp / (model + "-" + label + ".frames")
            env = dict(os.environ, ZPCW_MODEL=model, ZPCW_NO_FIXED_DUMPS="1",
                       ZPCW_STATE_SAVE=str(save), ZPCW_STATE_LOAD=str(restore),
                       ZPCW_FRAMEHASHES=str(hashes))
            cp = run([str(host), str(core.resolve()), str(disk), str(temp), str(frames)],
                     env=env, capture_output=True)
            if restore >= 0 and "unserialize @400 ok=1" not in cp.stderr:
                fail("mid-boot state was rejected: " + model)
            results.append([line.split()[1:] for line in hashes.read_text().splitlines()])
        if results[0][100:] != results[1][400:] or int(results[0][-1][1]) == 0:
            fail("restoring during boot did not reproduce the reference frames: " + model)
    # Same valid disk bytes under ordinary, exact helper-name and directory
    # collision paths must all be mounted from the external file.
    marked = bytearray(disk.read_bytes())
    marked[0x22:0x2e] = b"REVIEW-MARK!"
    for relative in ("custom.dsk", "openpcw_os.dsk", "openpcw_os.dsk-backup/custom.dsk"):
        content = temp / relative
        content.parent.mkdir(parents=True, exist_ok=True)
        content.write_bytes(marked)
        state = temp / "mounted.state"
        env = dict(os.environ, ZPCW_NO_FIXED_DUMPS="1", ZPCW_STATE_OUT=str(state))
        run([str(host), str(core.resolve()), str(content), str(temp), "1"],
            env=env, capture_output=True)
        blob = state.read_bytes()
        offset = 12 + int.from_bytes(blob[4:8], "little") + 76
        length = int.from_bytes(blob[offset+8:offset+12], "little")
        if blob[offset+12:offset+12+length] != marked:
            fail("external image replaced by embedded helper: " + relative)
    print("PASS: mid-boot states on both models and external helper-name paths")


def check_runtime(core: Path, cc: str, temp: Path, sanitizer: str, reloads: int) -> None:
    host_src = locate_test("host/libretro_host.c")
    host = temp / "libretro_host"
    flags = compiler_profile(sanitizer)
    run([cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra",
         "-Werror", *flags, str(host_src), "-ldl", "-o", str(host)])
    disk = ROOT / "src" / "openpcw_os.dsk"
    if not disk.is_file():
        fail("OpenPCW-OS test disk is missing")
    env = dict(os.environ)
    env.update({"ZPCW_OPTV2": "1", "ZPCW_OPTV2_FALSE": "1",
                "ZPCW_KEYMAP": "Custom",
                "ZPCW_OSKBUTTON": "B", "ZPCW_INPUTLOG": "1",
                "ZPCW_PAD": "0:2:3,0:5:6,2:8:10,3:8:10",
                "ZPCW_STATE_CORRUPT": "1", "ZPCW_DISK_CONTRACT": "1",
                "ZPCW_STATE_CONTRACT": "1",
                "ZPCW_RELOADS": str(reloads)})
    cp = run([str(host), str(core.resolve()), str(disk), str(ROOT / "src"), "20"],
             env=env, capture_output=True)
    output = cp.stdout + cp.stderr
    core_version = json.loads((ROOT / "src/pin.json").read_text())["version"]
    if not re.search(rf'^\[host\] core: .* {re.escape(core_version)}  ext=', output, re.M):
        print(output, file=sys.stderr)
        fail(f"loaded core does not report the pinned version {core_version}")
    required = (
        "retro_load_game -> 1", "option registration: v2=1 legacy=0",
        "physical keyboard options: toggle=1 rows=82 bad=0 hidden=82 shown=82",
        "input desc: id=0 -> 'On-screen keyboard'",
        "input desc: id=15 -> 'Quit to Title (EXIT)'",
        "OSK toggle button id=0 active=1", "OSK toggle button id=0 active=0",
        "slot 14 -> 'Quit to Title (EXIT)'",
        "corrupt-state rejection: rejected=1 unchanged=1",
        "disk-control contract: PASS", "save-state contract: PASS",
        f"lifecycle reloads passed: {reloads}",
        "clean shutdown + library unload",
    )
    for marker in required:
        if marker not in output:
            print(output, file=sys.stderr)
            fail(f"runtime marker missing: {marker}")
    check_loading_states(core, host, temp)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--platform", default="unix")
    ap.add_argument("--cc", default=os.environ.get("HOST_CC", "cc"),
                    help="native compiler for tests (HOST_CC, independent of the core's CC)")
    sanitizers = ap.add_mutually_exclusive_group()
    sanitizers.add_argument("--sanitize", action="store_true",
                            help="build the C tests/host with ASan+UBSan too")
    sanitizers.add_argument("--tsan", action="store_true",
                            help="build the C tests/host with ThreadSanitizer too")
    ap.add_argument("--reloads", type=int, default=10)
    args = ap.parse_args()
    core = Path(args.core)
    if not core.is_absolute():
        core = ROOT / core
    if not core.is_file():
        fail(f"core not found: {core}")
    sanitizer = "thread" if args.tsan else "address" if args.sanitize else ""
    check_metadata(); check_export_policy()
    check_exports(core, args.platform)
    with tempfile.TemporaryDirectory(prefix="zesarpcw-check-") as td:
        check_crop_unit(args.cc, Path(td), sanitizer)
        check_audio_unit(args.cc, Path(td), sanitizer)
        check_keyboard_unit(args.cc, Path(td), sanitizer)
        check_osk_unit(args.cc, Path(td), sanitizer)
        check_state_rle_unit(args.cc, Path(td), sanitizer)
        check_state_capacity_unit(args.cc, Path(td), sanitizer)
        cross_only = not native_runtime(args.platform)
        if not cross_only:
            check_runtime(core, args.cc, Path(td), sanitizer, args.reloads)
            if sys.platform.startswith("linux"):
                check_core_failures(args.cc, Path(td), sanitizer)
    suffix = "" if not cross_only else " (cross-build: runtime deferred to target)"
    print("CHECK PASS: metadata, exports, crop/audio/keyboard/OSK/state RLE/capacity"
          + (", disk/state/options and lifecycle" if not cross_only else "") + suffix)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
