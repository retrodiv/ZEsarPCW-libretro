#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GPLv3; see the core's LICENSE.
"""Reproduce the OpenPCW-OS resources and C headers used by this core.

With --source, verify and rebuild a matching standalone source tree using
Python and Pasmo. Without it, regenerate headers from the bundled disk/JSON.
--check compares the results without changing this repository.
All default paths are relative to this script. See SOURCE.md for instructions.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require_hash(data: bytes, expected: str, label: str) -> None:
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError(f"invalid SHA-256 for {label}")
    if sha256(data) != expected:
        raise ValueError(f"SHA-256 mismatch: {label}")


def read_manifest(data: bytes) -> dict[str, str]:
    """Read an exact inventory of normalized relative regular-file paths."""
    result = {}
    for line in data.decode("ascii").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            raise ValueError("malformed SHA-256 manifest line")
        digest, name = match.groups()
        path = PurePosixPath(name)
        if (path.is_absolute() or ".." in path.parts or "\\" in name
                or ":" in name or path.as_posix() != name or not path.parts
                or name in result):
            raise ValueError(f"unsafe or duplicate manifest path: {name}")
        result[name] = digest
    if not result:
        raise ValueError("empty SHA-256 manifest")
    return result


def read_regular(root: Path, name: str) -> bytes:
    path = root
    for part in PurePosixPath(name).parts:
        path = path / part
        if path.is_symlink():
            raise ValueError(f"linked source input: {name}")
    if not path.is_file():
        raise ValueError(f"missing source input: {name}")
    return path.read_bytes()


def verified_source(source: Path, version: str, manifest_sha256: str) -> dict[str, bytes]:
    manifest = read_regular(source, "SOURCE_MANIFEST.sha256")
    require_hash(manifest, manifest_sha256, "source manifest")
    files = {}
    for name, digest in read_manifest(manifest).items():
        data = read_regular(source, name)
        require_hash(data, digest, name)
        files[name] = data
    if files.get("VERSION", b"").decode("ascii").strip() != version:
        raise ValueError("source VERSION differs from the integration record")
    files["SOURCE_MANIFEST.sha256"] = manifest
    return files


def build_release(source: Path, version: str, manifest_sha256: str,
                  workdir: Path | None = None) -> dict[str, bytes]:
    """Build only from the verified inventory, without changing the caller's tree."""
    files = verified_source(source, version, manifest_sha256)
    checksums = files["dist/SHA256SUMS"]
    expected = read_manifest(checksums)
    if any(len(PurePosixPath(name).parts) != 1 for name in expected):
        raise ValueError("release checksums must name files at the release root")
    expected["SHA256SUMS"] = sha256(checksums)
    if workdir is not None:
        workdir = workdir.resolve()
    with tempfile.TemporaryDirectory(prefix="openpcw-rebuild-", dir=workdir) as td:
        temp = Path(td)
        snapshot = temp / "source"
        for name, data in files.items():
            target = snapshot / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        output = temp / "artifacts"
        subprocess.run([sys.executable, "-B", str(snapshot / "tools/build.py"),
                        "--output", str(output)], cwd=snapshot, check=True)
        artifacts = {}
        for name, digest in expected.items():
            data = read_regular(output, name)
            require_hash(data, digest, name)
            artifacts[name] = data
    return artifacts


def disk_header(disk: bytes) -> bytes:
    lines = ["/* SPDX-License-Identifier: GPL-3.0-only AND MIT */",
             "/* Generated from the pinned OpenPCW-OS release.",
             " * Data: Copyright (c) 2026 retrodiv <retrodiv@proton.me>, MIT licence.",
             " * Font data: Microsoft Corporation, MIT licence.",
             " * Preserve sources/openpcw-os/LICENSES.txt with copies of this data.",
             " * C array template: Copyright (c) 2026 retrodiv <retrodiv@proton.me>, GPLv3; see LICENSE.",
             " * The data retains its MIT terms independently of the generator.",
             " */",
             "static const unsigned char openpcw_os_dsk[] = {"]
    for start in range(0, len(disk), 20):
        lines.append("  " + ",".join(str(value) for value in disk[start:start + 20]) + ",")
    lines += ["};", ""]
    return "\n".join(lines).encode("ascii")


def integration_header(metadata_bytes: bytes, disk: bytes) -> bytes:
    metadata = json.loads(metadata_bytes.decode("ascii"))
    if (not isinstance(metadata, dict) or metadata.get("schema") != 1
            or metadata.get("system") != "OpenPCW-OS"
            or metadata.get("shell_ready_symbol") != "resident_shell_entry"
            or metadata.get("disk_sha256") != sha256(disk)):
        raise ValueError("integration metadata does not match its release disk")
    pc = metadata.get("shell_ready_pc")
    if type(pc) is not int or not 0 <= pc <= 0xFFFF:
        raise ValueError("shell-ready PC is outside the Z80 address space")
    return f"""/* SPDX-License-Identifier: GPL-3.0-only AND MIT */
/* Generated from the pinned OpenPCW-OS integration metadata.
 * Metadata: Copyright (c) 2026 retrodiv <retrodiv@proton.me>, MIT licence;
 * see sources/openpcw-os/LICENSE.
 * C header template: Copyright (c) 2026 retrodiv <retrodiv@proton.me>, GPLv3; see LICENSE.
 * The metadata retains its MIT terms independently of the generator.
 */
#ifndef OPENPCW_OS_INTEGRATION_H
#define OPENPCW_OS_INTEGRATION_H
#define OPENPCW_OS_SHELL_READY_PC 0x{pc:04X}
#endif
""".encode("ascii")


def integration_outputs(disk: bytes, metadata: bytes) -> dict[str, bytes]:
    # Validate the pair before producing any output.
    header = integration_header(metadata, disk)
    return {
        "src/openpcw_os.dsk": disk,
        "src/libretro/openpcw_os_disk_embed.h": disk_header(disk),
        "src/libretro/openpcw_os_integration.h": header,
    }


def read_record(bundle: Path) -> tuple[dict, dict[str, str]]:
    record = json.loads((bundle / "SOURCE.json").read_text(encoding="utf-8"))
    if (not isinstance(record, dict) or record.get("schema") != 1
            or record.get("project") != "OpenPCW-OS"
            or not isinstance(record.get("version"), str)
            or not isinstance(record.get("repository"), str)
            or not re.fullmatch(r"https://[^\s?#]+(?<!/)", record["repository"])
            or not isinstance(record.get("revision"), str)
            or not re.fullmatch(r"[0-9a-f]{40}", record["revision"])):
        raise ValueError("invalid OpenPCW-OS source record")
    if (bundle / "VERSION").read_text(encoding="ascii").strip() != record["version"]:
        raise ValueError("VERSION differs from the source record")
    source_manifest = (bundle / "SOURCE_MANIFEST.sha256").read_bytes()
    require_hash(source_manifest, record["source_manifest_sha256"], "source manifest")
    source_hashes = read_manifest(source_manifest)
    checksums = (bundle / "SHA256SUMS").read_bytes()
    require_hash(checksums, record["release_checksums_sha256"], "release checksums")
    require_hash(checksums, source_hashes["dist/SHA256SUMS"], "source release checksums")
    release_hashes = read_manifest(checksums)
    for name, digest in release_hashes.items():
        if source_hashes.get("dist/" + name) != digest:
            raise ValueError(f"source/release manifests disagree: {name}")
    for public, upstream in (("LICENSE", "LICENSE"),
                             ("LICENSE.microsoft-msdos", "third_party/microsoft-msdos/LICENSE"),
                             ("VERSION", "VERSION")):
        require_hash((bundle / public).read_bytes(), source_hashes[upstream], public)
    return record, release_hashes


def rebuild(core: Path, source: Path | None, check: bool,
            workdir: Path | None = None) -> None:
    bundle = core / "sources/openpcw-os"
    record, release_hashes = read_record(bundle)
    if source is not None:
        artifacts = build_release(source, record["version"],
                                  record["source_manifest_sha256"], workdir)
        for name, digest in release_hashes.items():
            require_hash(artifacts[name], digest, name)
        disk = artifacts["OpenPCW-OS.dsk"]
        metadata = artifacts["OpenPCW-OS-integration.json"]
        notices = artifacts["LICENSES.txt"]
    else:
        disk = (core / "src/openpcw_os.dsk").read_bytes()
        metadata = (bundle / "OpenPCW-OS-integration.json").read_bytes()
        notices = (bundle / "LICENSES.txt").read_bytes()
        for name, data in (("OpenPCW-OS.dsk", disk),
                           ("OpenPCW-OS-integration.json", metadata),
                           ("LICENSES.txt", notices)):
            require_hash(data, release_hashes[name], name)
    outputs = integration_outputs(disk, metadata)
    outputs["sources/openpcw-os/OpenPCW-OS-integration.json"] = metadata
    outputs["sources/openpcw-os/LICENSES.txt"] = notices
    if check:
        for name, data in outputs.items():
            if read_regular(core, name) != data:
                raise ValueError(f"generated resource differs: {name}")
    else:
        for name, data in outputs.items():
            target = core / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    print("OPENPCW CHECK PASS" if check else "OpenPCW-OS resources regenerated")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="matching standalone source checkout or extracted archive")
    parser.add_argument("--check", action="store_true", help="compare without writing core resources")
    parser.add_argument("--workdir", type=Path, help="existing directory for temporary rebuild files")
    args = parser.parse_args()
    core = Path(__file__).resolve().parents[2]
    try:
        rebuild(core, args.source, args.check, args.workdir)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
