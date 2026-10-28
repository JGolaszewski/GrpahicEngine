"""Focused tests for safe replacement of staged PRISM payload directories."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import prism_staging


class ReplaceDirectoryTests(unittest.TestCase):
    def test_replaces_old_tree_and_drops_stale_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination = root / "payload"
            staged = root / ".payload.staging"
            destination.mkdir()
            (destination / "old.txt").write_text("old")
            staged.mkdir()
            (staged / "new.txt").write_text("new")

            prism_staging.replace_directory(staged, destination)

            self.assertFalse((destination / "old.txt").exists())
            self.assertEqual((destination / "new.txt").read_text(), "new")
            self.assertEqual(list(root.glob(".payload.previous-*")), [])

    def test_restores_existing_tree_when_install_rename_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination = root / "payload"
            staged = root / ".payload.staging"
            destination.mkdir()
            (destination / "old.txt").write_text("old")
            staged.mkdir()
            (staged / "new.txt").write_text("new")
            real_replace = prism_staging.os.replace
            calls = 0

            def fail_second_replace(source: object, target: object) -> None:
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise OSError("simulated install rename failure")
                real_replace(source, target)

            with patch.object(prism_staging.os, "replace", side_effect=fail_second_replace):
                with self.assertRaisesRegex(OSError, "simulated install"):
                    prism_staging.replace_directory(staged, destination)

            self.assertEqual((destination / "old.txt").read_text(), "old")
            self.assertFalse((destination / "new.txt").exists())
            self.assertEqual(list(root.glob(".payload.previous-*")), [])


if __name__ == "__main__":
    unittest.main()
