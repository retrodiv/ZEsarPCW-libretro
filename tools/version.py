#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GPLv3; see LICENSE.
"""Keep the core pin, generated API header and commit version consistent."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PIN = "src/pin.json"
HEADER = "src/libretro/pcw_version.h"
API = "src/libretro/libretro.c"
INFO = "zesarpcw_libretro.info"
MANIFEST = "SOURCE_MANIFEST.sha256"


def parse_version(value: str) -> tuple[int, int, int]:
    if not isinstance(value, str) or not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", value):
        raise ValueError("version must be MAJOR.MINOR.PATCH without leading zeroes")
    return tuple(map(int, value.split(".")))


def next_version(value: str) -> str:
    major, minor, patch = parse_version(value)
    return f"{major}.{minor}.{patch + 1}"


def version_header(value: str) -> bytes:
    parse_version(value)
    return ("/* SPDX-License-Identifier: GPL-3.0-only */\n"
            "/* Copyright (c) 2026 retrodiv <retrodiv@proton.me>. */\n"
            "/* Generated from src/pin.json; use tools/version.py bump. */\n"
            "#ifndef PCW_VERSION_H\n#define PCW_VERSION_H\n"
            f'#define PCW_CORE_VERSION "{value}"\n'
            "#endif\n").encode()


def check_records(files: dict[str, bytes]) -> str:
    value = json.loads(files[PIN])["version"]
    parse_version(value)
    if files[HEADER] != version_header(value):
        raise ValueError("core pin and generated version header disagree")
    if not re.search(rb'\blibrary_version\s*=\s*PCW_CORE_VERSION\s*;', files[API]):
        raise ValueError("core API must use PCW_CORE_VERSION")
    if not re.search(rb'^#include "pcw_version.h"$', files[API], re.M):
        raise ValueError("core API must include pcw_version.h")
    if not re.search(rb'^display_version\s*=\s*"Git"\s*$', files[INFO], re.M):
        raise ValueError('core .info display_version must be "Git"')
    return value


def manifest_entries(data: bytes) -> dict[str, str]:
    entries = {}
    for line in data.decode().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            raise ValueError("malformed source manifest")
        digest, name = match.groups()
        path = Path(name)
        if path.is_absolute() or ".." in path.parts or ".git" in path.parts or name in (MANIFEST, "AGENTS.md") or name in entries:
            raise ValueError(f"invalid source manifest entry: {name}")
        entries[name] = digest
    return entries


def bump(root: Path, dry_run: bool = False) -> str:
    entries = manifest_entries((root / MANIFEST).read_bytes())
    # Include newly staged source files and deletions; untracked local files
    # cannot become package inputs merely because a version was advanced.
    if (root / ".git").exists():
        tracked = subprocess.check_output(["git", "-C", str(root), "ls-files", "-z"])
        entries = {name: "" for name in tracked.decode().split("\0")
                   if name and name not in (MANIFEST, "AGENTS.md")}
    files = {}
    for name in entries:
        path = root / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"source is missing or linked: {name}")
        files[name] = path.read_bytes()
    previous = check_records(files)
    value = next_version(previous)
    pin = json.loads(files[PIN])
    pin["version"] = value
    files[PIN] = (json.dumps(pin, indent=2) + "\n").encode()
    files[HEADER] = version_header(value)
    manifest = "".join(f"{hashlib.sha256(files[name]).hexdigest()}  {name}\n"
                       for name in sorted(entries)).encode()
    if not dry_run:
        # Write in place: renaming files on network filesystems can corrupt them.
        for name in (PIN, HEADER):
            (root / name).write_bytes(files[name])
        (root / MANIFEST).write_bytes(manifest)
    print(f"{'Would bump' if dry_run else 'Version raised'}: {previous} -> {value}")
    return value


def check_commit(root: Path) -> None:
    def git(*args, check=True):
        return subprocess.run(["git", "-C", str(root), *args], capture_output=True, check=check).stdout

    gitdir = Path(git("rev-parse", "--absolute-git-dir").decode().strip())
    for marker in ("MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD", "rebase-merge", "rebase-apply"):
        if (gitdir / marker).exists():
            return
    files = {name: git("show", f":{name}") for name in (PIN, HEADER, API, INFO, MANIFEST)}
    value = check_records(files)
    previous_pin = git("show", f"HEAD:{PIN}", check=False)
    if previous_pin:
        previous = json.loads(previous_pin)["version"]
    else:
        # The first version-policy commit upgrades the earlier literal API version.
        previous_api = git("show", f"HEAD:{API}", check=False)
        match = re.search(rb'library_version\s*=\s*"([0-9.]+)"', previous_api)
        previous = match[1].decode() if match else None
    if previous is not None and value != next_version(previous):
        raise ValueError(f"each normal commit must bump {previous} to {next_version(previous)}; run the bump command before staging")
    entries = manifest_entries(files[MANIFEST])
    tracked = set(git("ls-files", "-z").decode().split("\0")) - {"", MANIFEST, "AGENTS.md"}
    if tracked != set(entries):
        raise ValueError("staged source inventory differs from SOURCE_MANIFEST.sha256")
    # Hash index blobs, so unstaged edits cannot conceal a stale staged manifest.
    request = "".join(f":{name}\n" for name in entries).encode()
    blobs = subprocess.run(["git", "-C", str(root), "cat-file", "--batch"],
                           input=request, capture_output=True, check=True).stdout
    offset = 0
    for name, expected in entries.items():
        end = blobs.index(b"\n", offset)
        fields = blobs[offset:end].split()
        if len(fields) != 3 or fields[1] != b"blob":
            raise ValueError(f"staged source missing: {name}")
        size = int(fields[2])
        data = blobs[end + 1:end + 1 + size]
        offset = end + size + 2
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f"staged source manifest is stale: {name}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("bump", "check", "pre-commit"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        if args.command == "bump":
            bump(ROOT, args.dry_run)
        elif args.command == "pre-commit":
            check_commit(ROOT)
        else:
            value = check_records({name: (ROOT / name).read_bytes() for name in (PIN, HEADER, API, INFO)})
            print(f"Core version: {value}; .info: Git")
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"VERSION CHECK FAILED: {error}\n")


if __name__ == "__main__":
    main()
