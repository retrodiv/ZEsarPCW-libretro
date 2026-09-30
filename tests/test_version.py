#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GPLv3; see LICENSE.
"""Version and staged-commit regression guards using disposable repositories."""
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
POLICY = runpy.run_path(str(ROOT / "tools/version.py"))


class VersionTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="zpcw-version-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.files = {
            POLICY["PIN"]: b'{"version": "13.0.9"}\n',
            POLICY["HEADER"]: POLICY["version_header"]("13.0.9"),
            POLICY["API"]: b'#include "pcw_version.h"\ninfo->library_version = PCW_CORE_VERSION;\n',
            POLICY["INFO"]: b'display_version = "Git"\n',
            "tools/version.py": (ROOT / "tools/version.py").read_bytes(),
            ".githooks/pre-commit": (ROOT / ".githooks/pre-commit").read_bytes(),
        }
        for name, data in self.files.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (self.root / ".githooks/pre-commit").chmod(0o755)
        self.manifest()
        self.git("init", "-q")
        self.git("config", "user.name", "retrodiv")
        self.git("config", "user.email", "retrodiv@proton.me")
        self.git("add", ".")
        self.git("commit", "-qm", "test: initialize disposable version fixture")
        self.git("config", "core.hooksPath", ".githooks")

    def git(self, *args, check=True):
        return subprocess.run(["git", "-C", str(self.root), *args],
                              capture_output=True, text=True, check=check)

    def manifest(self):
        (self.root / POLICY["MANIFEST"]).write_text("".join(
            f"{hashlib.sha256((self.root/name).read_bytes()).hexdigest()}  {name}\n"
            for name in sorted(self.files)))

    def attempt(self):
        self.git("add", ".")
        return self.git("commit", "--allow-empty", "-qm", "test: disposable version change", check=False)

    def test_bump_advances_decimal_patch_and_preserves_upstream(self):
        pin_path = self.root / POLICY["PIN"]
        pin_path.write_text(json.dumps({"version": "13.0.9", "upstream": {"version": "13.0"}}))
        POLICY["bump"](self.root)
        pin = json.loads(pin_path.read_text())
        self.assertEqual(pin, {"version": "13.0.10", "upstream": {"version": "13.0"}})
        self.assertEqual(self.attempt().returncode, 0)

    def test_dry_run_does_not_modify_any_file(self):
        before = {name: path.read_bytes() for path in self.root.rglob("*")
                  if path.is_file() for name in [path.relative_to(self.root).as_posix()]}
        self.assertEqual(POLICY["bump"](self.root, True), "13.0.10")
        after = {name: (self.root/name).read_bytes() for name in before}
        self.assertEqual(before, after)

    def test_unchanged_version_is_rejected(self):
        result = self.attempt()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must bump", result.stderr)

    def test_invalid_versions_leave_sources_unchanged(self):
        for value in ("13.00.9", "13.0.09", "13.0", "Git", "13.0.-1"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                POLICY["next_version"](value)

    def test_backwards_or_skipped_patch_is_rejected(self):
        for value in ("13.0.8", "13.0.11", "14.0.0"):
            with self.subTest(value=value):
                (self.root / POLICY["PIN"]).write_text(json.dumps({"version": value}))
                (self.root / POLICY["HEADER"]).write_bytes(POLICY["version_header"](value))
                self.manifest()
                self.assertIn("must bump", self.attempt().stderr)

    def test_mismatched_header_or_metadata_is_rejected(self):
        POLICY["bump"](self.root)
        path = self.root / POLICY["HEADER"]
        path.write_bytes(POLICY["version_header"]("13.0.9"))
        self.assertIn("disagree", self.attempt().stderr)
        path.write_bytes(POLICY["version_header"]("13.0.10"))
        (self.root / POLICY["INFO"]).write_text('display_version = "13.0.10"\n')
        self.assertIn('must be "Git"', self.attempt().stderr)

    def test_stale_staged_manifest_is_rejected_despite_unstaged_repair(self):
        POLICY["bump"](self.root)
        path = self.root / POLICY["API"]
        path.write_bytes(self.files[POLICY["API"]] + b"/* staged edit */\n")
        self.git("add", ".")
        self.manifest()  # Working manifest agrees, but the staged one is stale.
        result = self.git("commit", "-qm", "test: stale fixture manifest", check=False)
        self.assertIn("manifest is stale", result.stderr)

    def test_initial_policy_commit_checks_previous_literal_version(self):
        self.git("config", "core.hooksPath", ".git/hooks")
        self.git("rm", POLICY["PIN"], POLICY["HEADER"])
        (self.root / POLICY["API"]).write_text('info->library_version = "13.0.9";\n')
        self.git("add", ".")
        self.git("commit", "-qm", "test: earlier literal version fixture")
        for name in (POLICY["PIN"], POLICY["HEADER"], POLICY["API"]):
            (self.root / name).write_bytes(self.files[name])
        self.git("config", "core.hooksPath", ".githooks")
        self.assertIn("must bump", self.attempt().stderr)
        POLICY["bump"](self.root)
        self.assertEqual(self.attempt().returncode, 0)

    def test_history_operation_does_not_require_another_bump(self):
        (self.root / ".git/CHERRY_PICK_HEAD").write_text(self.git("rev-parse", "HEAD").stdout)
        self.git("add", ".")
        POLICY["check_commit"](self.root)

    def test_source_pin_and_package_version_agree_while_info_says_git(self):
        package = runpy.run_path(str(ROOT / "package.py"))
        files, _ = package["capture_source"](ROOT, True)
        self.assertEqual(package["version"](files), json.loads(files[POLICY["PIN"]])["version"])
        files[POLICY["HEADER"]] = POLICY["version_header"]("99.0.0")
        with self.assertRaisesRegex(ValueError, "disagree"):
            package["version"](files)


if __name__ == "__main__":
    unittest.main()
