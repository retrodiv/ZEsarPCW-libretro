#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GPLv3; see LICENSE.
"""Build a checked core from the exact source archive shipped in its package.

Python 3.10+, make, a target C compiler and strip are required. See PACKAGING.md.
No prebuilt library is accepted. Nothing is downloaded or published.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent
MANIFEST = "SOURCE_MANIFEST.sha256"
NAME = "zesarpcw_libretro"
# make platform, checker platform, compiler, strip, extension
TARGETS = {
    "linux-x86_64": ("unix", "unix", "gcc", "strip", ".so"),
    "linux-aarch64": ("linux-aarch64", "linux-aarch64", "aarch64-linux-gnu-gcc", "aarch64-linux-gnu-strip", ".so"),
    "windows-x86_64": ("win", "win", "x86_64-w64-mingw32-gcc", "x86_64-w64-mingw32-strip", ".dll"),
    "android-arm64": ("android", "android-arm64", None, None, ".so"),
    "macos-x86_64": ("osx-x86_64", "osx-x86_64", "clang", "strip", ".dylib"),
    "macos-arm64": ("osx-arm64", "osx-arm64", "clang", "strip", ".dylib"),
}


def source_helper(root: Path):
    spec = importlib.util.spec_from_file_location("openpcw_rebuild", root / "sources/openpcw-os/rebuild.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def capture_source(root: Path, snapshot: bool) -> tuple[dict[str, bytes], dict]:
    helper = source_helper(root)
    manifest = helper.read_regular(root, MANIFEST)
    files = {}
    for name, expected in helper.read_manifest(manifest).items():
        if name == MANIFEST or ".git" in Path(name).parts:
            raise ValueError(f"invalid source inventory entry: {name}")
        data = helper.read_regular(root, name)
        helper.require_hash(data, expected, name)
        files[name] = data
    files[MANIFEST] = manifest
    identity = {"kind": "snapshot" if snapshot else "commit", "commit": None,
                "manifest_sha256": digest(manifest)}
    if not snapshot:
        def git(*args):
            return subprocess.check_output(["git", "-C", str(root), *args])
        try:
            commit = git("rev-parse", "--verify", "HEAD^{commit}").decode().strip()
            entries = git("ls-tree", "-rz", "--full-tree", commit).split(b"\0")
            tracked = {}
            for entry in filter(None, entries):
                meta, name = entry.split(b"\t", 1)
                mode, kind, oid = meta.split()
                name = name.decode("utf-8")
                if mode not in (b"100644", b"100755") or kind != b"blob":
                    raise ValueError(f"non-regular committed input: {name}")
                tracked[name] = oid.decode()
            if tracked.keys() != files.keys():
                raise ValueError("commit file inventory differs from the source manifest")
            for name, data in files.items():
                blob = b"blob " + str(len(data)).encode() + b"\0" + data
                algorithm = "sha1" if len(tracked[name]) == 40 else "sha256"
                if hashlib.new(algorithm, blob).hexdigest() != tracked[name]:
                    raise ValueError(f"source differs from commit: {name}")
            subprocess.run(["git", "-C", str(root), "diff", "--cached", "--quiet", commit], check=True)
            subprocess.run(["git", "-C", str(root), "diff", "--quiet", commit], check=True)
            identity["commit"] = commit
        except subprocess.CalledProcessError as exc:
            raise ValueError("a clean committed source is required; use --snapshot for a local development package") from exc
    return files, identity


def source_archive(files: dict[str, bytes], prefix: str) -> bytes:
    raw = io.BytesIO()
    with gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
            for name, data in sorted(files.items()):
                info = tarfile.TarInfo(f"{prefix}/{name}")
                info.size = len(data)
                info.mode = 0o644
                archive.addfile(info, io.BytesIO(data))
    return raw.getvalue()


def write_source(files: dict[str, bytes], root: Path) -> None:
    for name, data in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def checksums(files: dict[str, bytes]) -> bytes:
    return "".join(f"{digest(data)}  {name}\n" for name, data in sorted(files.items())).encode()


def write_zip(path: Path, files: dict[str, bytes]) -> None:
    # All inputs already live in memory; output is created only after checks pass.
    with path.open("xb") as output:
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(files.items()):
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, data)


def version(files: dict[str, bytes]) -> str:
    info = re.search(rb'^display_version\s*=\s*"([A-Za-z0-9.+-]+)"', files[NAME + ".info"], re.M)
    api = re.search(rb'library_version\s*=\s*"([A-Za-z0-9.+-]+)"', files["src/libretro/libretro.c"])
    if not info or not api or info[1] != api[1]:
        raise ValueError("core API and .info versions must agree")
    return info[1].decode()


def notice_files(files: dict[str, bytes]) -> dict[str, bytes]:
    names = ["LICENSE", "AUTHORS", "PACKAGING.md", "src/LICENSES_info",
             "sources/fdc/README.md"]
    names += [name for name in files if name.startswith("licenses/")]
    # Full reconstruction records already travel in the core source archive.
    # Keep the complete MIT notices and immutable upstream pointer beside the binary.
    names += ["sources/openpcw-os/LICENSES.txt", "sources/openpcw-os/SOURCE.json"]
    notices = {name: files[name] for name in names}
    # Preserve all leading notices, including SPDX and component attribution.
    for source, target in [("src/libretro/libretro.h", "licenses/LIBRETRO-API.txt"),
                           ("src/libretro/pcw_fdc_samples.h", "licenses/FDC-SAMPLES.txt")]:
        data = files[source]
        notice = re.match(rb"(?:/\*.*?\*/\s*)+", data, re.S)
        if not notice:
            raise ValueError(f"missing leading licence notice: {source}")
        notices[target] = notice[0].rstrip() + b"\n"
    return notices


def build_package(args) -> Path:
    files, identity = capture_source(ROOT, args.snapshot)
    ver = version(files)
    source_id = identity["commit"] or ("snapshot-" + identity["manifest_sha256"])
    stem = f"ZEsarPCW-libretro-{ver}-{source_id}"
    source_name = stem + "-source.tar.gz"
    payload = notice_files(files)
    payload[source_name] = source_archive(files, stem + "-source")
    payload[NAME + ".info"] = files[NAME + ".info"]
    helper = source_helper(ROOT)
    guest, _ = helper.read_record(ROOT / "sources/openpcw-os")
    guest_archive = None
    if args.openpcw_source:
        guest_files = helper.verified_source(args.openpcw_source, guest["version"], guest["source_manifest_sha256"])
        guest_stem = "OpenPCW-OS-" + guest["version"] + "-" + guest["source_manifest_sha256"]
        guest_archive = guest_stem + "-source.tar.gz"
        payload[guest_archive] = source_archive(guest_files, guest_stem)
    platform, check_platform, cc, strip, ext = TARGETS[args.platform]
    cc, strip = args.cc or cc, args.strip or strip
    if not cc or not strip:
        raise ValueError("Android requires --cc and --strip from the selected NDK")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    archive_path = out / (stem + "-" + args.platform + ".zip")
    if archive_path.exists():
        raise ValueError(f"package already exists: {archive_path.name}")
    # Fresh inputs, no stale objects/prebuilt library or ambient make overrides.
    env = os.environ.copy()
    for key in ("MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "MAKEFILES", "CC", "CPATH",
                "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH"):
        env.pop(key, None)
    env["SOURCE_DATE_EPOCH"] = "0"
    env["LC_ALL"] = "C"
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    make = ["make", f"platform={platform}", f"CC={cc}", "CFLAGS=-O2",
            "CPPFLAGS=", "LDFLAGS=", "LDLIBS=", "all", f"-j{args.jobs}"]
    strip_flags = ["-x"] if args.platform.startswith("macos-") else ["--strip-unneeded"]
    with tempfile.TemporaryDirectory(prefix="zesarpcw-package-", dir=args.workdir) as temp:
        build = Path(temp) / "source"
        write_source(files, build)
        # Validate the guest integration from the frozen inputs as well.
        source_helper(build).rebuild(build, source=None, check=True)
        # Fail on a mislabeled host/toolchain even where cross-only check.py
        # cannot execute the target library.
        machine = subprocess.check_output([cc, "-dumpmachine"], env=env, text=True).strip()
        arch = "aarch64|arm64" if args.platform.endswith(("aarch64", "arm64")) else "x86_64|amd64"
        os_pattern = {"linux": "linux", "windows": "mingw|windows", "android": "android", "macos": "darwin|apple"}[args.platform.split("-")[0]]
        if (not re.match(f"^(?:{arch})-", machine) or not re.search(os_pattern, machine)
                or (args.platform.startswith("linux-") and "android" in machine)):
            raise ValueError(f"compiler target {machine} does not match {args.platform}")
        compiler = subprocess.check_output([cc, "--version"], env=env, text=True).splitlines()[0]
        subprocess.run(make, cwd=build, env=env, check=True)
        binary = build / (NAME + ext)
        debug = binary.read_bytes() if args.debug_output else None
        subprocess.run([strip, *strip_flags, str(binary)], cwd=build, env=env, check=True)
        subprocess.run([sys.executable, "check.py", "--core", "./" + binary.name,
                        "--platform", check_platform], cwd=build, env=env, check=True)
        payload[binary.name] = binary.read_bytes()
    identity.update({"archive": source_name, "archive_sha256": digest(payload[source_name])})
    record = {"schema": 1, "project": "ZEsarPCW-libretro", "version": ver,
              "platform": args.platform, "source": identity,
              "binary": {"file": NAME + ext, "sha256": digest(payload[NAME + ext])},
              "openpcw_os": {**guest, "archive": guest_archive,
                             "archive_sha256": digest(payload[guest_archive]) if guest_archive else None},
              "build": {"make_platform": platform, "compiler": Path(cc).name,
                        "compiler_version": compiler, "compiler_target": machine,
                        "cflags": "-O2", "cppflags": "", "ldflags": "", "ldlibs": "",
                        "source_date_epoch": 0, "strip": Path(strip).name,
                        "strip_flags": strip_flags, "check_platform": check_platform}}
    payload["RELEASE.json"] = (json.dumps(record, indent=2, sort_keys=True) + "\n").encode()
    guest_note = (f"OpenPCW-OS source: {guest['repository']}/tree/{guest['revision']}\n"
                  f"Source manifest SHA-256: {guest['source_manifest_sha256']}\n"
                  "The complete OpenPCW-OS/Microsoft MIT notices accompany the binary.\n")
    if guest_archive:
        guest_note += f"Optional OpenPCW-OS source archive: {guest_archive}\n"
    payload["README.txt"] = (f"ZEsarPCW-libretro {ver} / {args.platform}\n"
        f"Source identity: {source_id}\n"
        f"Core source: {source_name}\n\n"
        "Keep this ZIP intact when redistributing it. It includes the core GPL,\n"
        "AUTHORS, hardware identification/provenance, sound and libretro API notices,\n"
        "and the full OpenPCW-OS/Microsoft MIT notices. See PACKAGING.md.\n"
        "Extract the core source archive for build instructions and editable assets.\n"
        "RELEASE.json binds the binary, source archive and build recipe by SHA-256;\n"
        "SHA256SUMS covers every package file except itself.\n\n" + guest_note).encode()
    payload["SHA256SUMS"] = checksums(payload)
    write_zip(archive_path, payload)
    if args.debug_output:
        args.debug_output.parent.mkdir(parents=True, exist_ok=True)
        args.debug_output.write_bytes(debug)
    print(f"Package: {archive_path}")
    print(f"SHA-256: {digest(archive_path.read_bytes())}")
    return archive_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=TARGETS, required=True)
    parser.add_argument("--snapshot", action="store_true", help="label an uncommitted development inventory by its SHA-256")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--openpcw-source", type=Path, help="include the verified standalone preferred source as a separate archive")
    parser.add_argument("--cc", help="target compiler executable (one path, no shell arguments)")
    parser.add_argument("--strip", help="target strip executable")
    parser.add_argument("--debug-output", type=Path, help="private unstripped companion, kept outside the package")
    parser.add_argument("--workdir", type=Path, help="parent directory for the temporary build")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 4))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        build_package(args)
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as exc:
        print(f"package: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
