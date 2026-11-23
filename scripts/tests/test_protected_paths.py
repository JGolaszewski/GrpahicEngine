"""Unit tests for the protected-path pull-request gate."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_protected_paths


class ProtectedPathsTests(unittest.TestCase):
    old_oid = "10830d7025d979a24a4f765fc0ee18ce0602dbae"
    new_oid = "bf0bbd9b666a7297492beb1999a4ddc0dfded143"

    def test_raw_git_diff_parser_preserves_gitlink_modes_and_path(self) -> None:
        output = (
            f":160000 160000 {self.old_oid} {self.new_oid} M\0vendor/nvrhi\0".encode()
        )

        self.assertEqual(
            check_protected_paths.parse_raw_diff_entries(output),
            [
                (
                    "160000",
                    "160000",
                    "10830d7025d979a24a4f765fc0ee18ce0602dbae",
                    "bf0bbd9b666a7297492beb1999a4ddc0dfded143",
                    "M",
                    "vendor/nvrhi",
                )
            ],
        )

    def test_nvrhi_gitlink_pointer_update_is_allowed(self) -> None:
        entry = ("160000", "160000", self.old_oid, self.new_oid, "M", "vendor/nvrhi")

        self.assertFalse(check_protected_paths.is_protected_change(entry))

    def test_empty_raw_diff_is_a_valid_no_change_result(self) -> None:
        self.assertEqual(check_protected_paths.parse_raw_diff_entries(b""), [])

    def test_nvrhi_exception_requires_a_real_pointer_change_at_exact_path(self) -> None:
        allowed_pointer = ("160000", "160000", self.old_oid, self.new_oid, "M", "vendor/nvrhi")
        blocked_entries = (
            ("160000", "160000", self.old_oid, self.old_oid, "M", "vendor/nvrhi"),
            ("100644", "100644", self.old_oid, self.new_oid, "M", "vendor/nvrhi"),
            ("160000", "160000", self.old_oid, self.new_oid, "M", "Vendor/NVRHI"),
            ("160000", "160000", self.old_oid, self.new_oid, "M", "vendor/nvrhi/README.md"),
        )

        self.assertFalse(check_protected_paths.is_protected_change(allowed_pointer))
        for entry in blocked_entries:
            with self.subTest(entry=entry):
                self.assertTrue(check_protected_paths.is_protected_change(entry))

    def test_vendor_source_edits_and_other_gitlinks_remain_blocked(self) -> None:
        entries = (
            ("100644", "100644", "old", "new", "M", "vendor/nvrhi/include/nvrhi.h"),
            ("160000", "160000", "old", "new", "M", "vendor/imgui"),
            ("000000", "160000", "000000", "new", "A", "vendor/nvrhi"),
            ("160000", "000000", "old", "000000", "D", "vendor/nvrhi"),
        )

        for entry in entries:
            with self.subTest(entry=entry):
                self.assertTrue(check_protected_paths.is_protected_change(entry))

    def test_generated_build_paths_remain_blocked(self) -> None:
        entry = ("100644", "100644", "old", "new", "M", "out/build/generated.cpp")

        self.assertTrue(check_protected_paths.is_protected_change(entry))

    def test_malformed_raw_diff_is_rejected(self) -> None:
        malformed_outputs = (
            b"garbage",
            b":160000 160000 " + self.old_oid.encode() + b" " + self.new_oid.encode() + b" M\0vendor/nvrhi",
            b"160000 160000 " + self.old_oid.encode() + b" " + self.new_oid.encode() + b" M\0vendor/nvrhi\0",
            b":160000 160000 " + self.old_oid.encode() + b" " + self.new_oid.encode() + b" M\0",
            b":160000 notmode " + self.old_oid.encode() + b" " + self.new_oid.encode() + b" M\0vendor/nvrhi\0",
            b":160000 160000 short " + self.new_oid.encode() + b" M\0vendor/nvrhi\0",
            b":160000 160000 " + self.old_oid.encode() + b" " + self.new_oid.encode() + b" X\0vendor/nvrhi\0",
            b":160000 160000 " + self.old_oid.encode() + b" " + self.new_oid.encode() + b" M\0vendor/nvrhi\0orphan\0",
        )

        for output in malformed_outputs:
            with self.subTest(output=output):
                with self.assertRaises(ValueError):
                    check_protected_paths.parse_raw_diff_entries(output)


if __name__ == "__main__":
    unittest.main()
