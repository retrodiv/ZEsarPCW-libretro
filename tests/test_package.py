#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GPLv3; see LICENSE.
"""Distribution regressions; independent of the emulator runtime checker."""
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("package", ROOT / "package.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class PackageTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="zpcw-package-test-")
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name) / "core"
        self.files, _ = PACKAGE.capture_source(ROOT, snapshot=True)
        PACKAGE.write_source(self.files, self.root)

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], stderr=subprocess.STDOUT)

    def commit(self):
        self.git("init", "-q")
        self.git("add", ".")
        self.git("-c", "user.name=Package test", "-c", "user.email=package@example.invalid",
                 "commit", "-qm", "Temporary packaging fixture")
        return self.git("rev-parse", "HEAD").decode().strip()

    def refresh(self):
        self.files = {name: (self.root / name).read_bytes() for name in self.files if name != PACKAGE.MANIFEST}
        self.files[PACKAGE.MANIFEST] = PACKAGE.checksums(self.files)
        (self.root / PACKAGE.MANIFEST).write_bytes(self.files[PACKAGE.MANIFEST])

    def test_clean_commit_and_source_archive_have_exact_inputs(self):
        commit = self.commit()
        # Files outside the declared source inventory cannot enter the archive.
        (self.root / "private-local.txt").write_text("not a package input")
        (self.root / "stale.so").write_bytes(b"old binary")
        files, record = PACKAGE.capture_source(self.root, snapshot=False)
        self.assertEqual(record["commit"], commit)
        data = PACKAGE.source_archive(files, "source")
        self.assertEqual(data, PACKAGE.source_archive(files, "source"))
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
            contents = {p.name.removeprefix("source/"): archive.extractfile(p).read() for p in archive}
        self.assertEqual(contents, self.files)

    def test_edited_file_requires_updated_manifest(self):
        (self.root / "README.md").write_text("a local edit")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            PACKAGE.capture_source(self.root, snapshot=True)

    def test_updated_manifest_cannot_masquerade_as_old_commit(self):
        self.commit()
        (self.root / "README.md").write_text("a local edit")
        self.refresh()
        with self.assertRaisesRegex(ValueError, "source differs from commit"):
            PACKAGE.capture_source(self.root, snapshot=False)
        files, record = PACKAGE.capture_source(self.root, snapshot=True)
        self.assertIsNone(record["commit"])
        self.assertEqual(record["manifest_sha256"], PACKAGE.digest(files[PACKAGE.MANIFEST]))

    def test_staged_changes_are_rejected_even_if_working_bytes_match_head(self):
        self.commit()
        path = self.root / "README.md"
        path.write_text("staged edit")
        self.git("add", "README.md")
        path.write_bytes(self.files["README.md"])
        with self.assertRaisesRegex(ValueError, "clean committed source"):
            PACKAGE.capture_source(self.root, snapshot=False)

    def test_tracked_file_missing_from_manifest_is_rejected(self):
        (self.root / "unexpected.txt").write_text("tracked but not in source inventory")
        self.commit()
        with self.assertRaisesRegex(ValueError, "file inventory differs"):
            PACKAGE.capture_source(self.root, snapshot=False)

    def test_changed_executable_mode_is_not_a_clean_commit(self):
        self.commit()
        self.git("config", "core.filemode", "true")
        path = self.root / "package.py"
        path.chmod(path.stat().st_mode ^ 0o111)
        with self.assertRaisesRegex(ValueError, "clean committed source"):
            PACKAGE.capture_source(self.root, snapshot=False)

    def test_source_symlinks_are_rejected(self):
        path = self.root / "README.md"
        path.unlink()
        path.symlink_to(ROOT / "README.md")
        with self.assertRaisesRegex(ValueError, "linked source"):
            PACKAGE.capture_source(self.root, snapshot=True)

    def test_notices_preserve_original_component_texts(self):
        notices = PACKAGE.notice_files(self.files)
        for name in ("LICENSE", "AUTHORS", "licenses/AMSTRAD.md",
                     "sources/openpcw-os/LICENSES.txt", "sources/fdc/README.md"):
            self.assertEqual(notices[name], self.files[name])
        guest_notices = notices["sources/openpcw-os/LICENSES.txt"]
        for name in ("LICENSE", "LICENSE.microsoft-msdos"):
            self.assertIn(self.files["sources/openpcw-os/" + name].strip(), guest_notices)
        self.assertEqual({name for name in notices if name.startswith("sources/openpcw-os/")},
                         {"sources/openpcw-os/LICENSES.txt", "sources/openpcw-os/SOURCE.json"})
        self.assertIn(b"2010-2024 The RetroArch team", notices["licenses/LIBRETRO-API.txt"])
        self.assertIn(b"SOFTWARE IS PROVIDED", notices["licenses/LIBRETRO-API.txt"])
        self.assertIn(b"2004-2005 David Colmenero", notices["licenses/FDC-SAMPLES.txt"])
        self.assertIn(b"any later version", notices["licenses/FDC-SAMPLES.txt"])

    def test_failed_build_creates_no_package(self):
        out = self.root.parent / "output"
        args = SimpleNamespace(snapshot=True, openpcw_source=None, platform="linux-x86_64",
                               cc=None, strip=None, output=out, jobs=1, workdir=None,
                               debug_output=None)
        # Compiler identification succeeds, but the actual make command fails.
        with patch.object(PACKAGE, "ROOT", self.root), \
             patch.object(PACKAGE.subprocess, "check_output", side_effect=["x86_64-linux-gnu", "gcc test"]), \
             patch.object(PACKAGE.subprocess, "run", side_effect=subprocess.CalledProcessError(2, "make")):
            with self.assertRaises(subprocess.CalledProcessError):
                PACKAGE.build_package(args)
        self.assertEqual(list(out.iterdir()), [])

    def test_guest_source_reference_requires_a_repository_and_immutable_revision(self):
        bundle = self.root / "sources/openpcw-os"
        path = bundle / "SOURCE.json"
        record = json.loads(path.read_text())
        helper = PACKAGE.source_helper(self.root)
        self.assertEqual(helper.read_record(bundle)[0], record)
        for field, value in (("repository", ""), ("repository", "pending"),
                             ("revision", "main"), ("revision", None)):
            with self.subTest(field=field, value=value):
                path.write_text(json.dumps({**record, field: value}))
                with self.assertRaisesRegex(ValueError, "invalid OpenPCW-OS source record"):
                    helper.read_record(bundle)


if __name__ == "__main__":
    unittest.main()
