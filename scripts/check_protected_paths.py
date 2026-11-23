"""Reject pull requests that modify vendored or generated engine outputs."""

from __future__ import annotations

import re
import subprocess
import sys


PROTECTED_PARTS = {"vendor", "out", "build", "bin", "artifacts", "cmakefiles"}
PROTECTED_SUFFIXES = {
    ".dxbc",
    ".cso",
    ".spv",
    ".rdc",
    ".obj",
    ".o",
    ".pdb",
    ".ilk",
    ".lib",
    ".dll",
    ".exe",
}


def parse_raw_diff_entries(output: bytes) -> list[tuple[str, str, str, str, str, str]]:
    """Return old mode, new mode, old OID, new OID, status, and path for each diff entry."""
    if not output:
        return []
    if not output.endswith(b"\0"):
        raise ValueError("raw git diff output is missing its terminal NUL")

    tokens = output[:-1].split(b"\0")
    if len(tokens) % 2:
        raise ValueError("raw git diff output has an incomplete metadata/path record")

    entries: list[tuple[str, str, str, str, str, str]] = []
    for index in range(0, len(tokens), 2):
        try:
            metadata = tokens[index].decode("ascii")
        except UnicodeDecodeError as error:
            raise ValueError("raw git diff metadata is not ASCII") from error
        path = tokens[index + 1].decode("utf-8", errors="surrogateescape").replace("\\", "/")
        if not path:
            raise ValueError("raw git diff entry has an empty path")
        if not metadata.startswith(":"):
            raise ValueError(f"raw git diff metadata is missing its ':' prefix: {metadata!r}")
        fields = metadata[1:].split()
        if len(fields) != 5:
            raise ValueError(f"unexpected raw git diff metadata: {metadata!r}")
        old_mode, new_mode, old_oid, new_oid, status = fields
        if not re.fullmatch(r"[0-7]{6}", old_mode) or not re.fullmatch(r"[0-7]{6}", new_mode):
            raise ValueError(f"raw git diff has an invalid file mode: {metadata!r}")
        if (
            len(old_oid) not in (40, 64)
            or len(new_oid) != len(old_oid)
            or not re.fullmatch(r"[0-9a-fA-F]+", old_oid)
            or not re.fullmatch(r"[0-9a-fA-F]+", new_oid)
        ):
            raise ValueError(f"raw git diff has an invalid or abbreviated object ID: {metadata!r}")
        if status not in {"A", "C", "D", "M", "R", "T"}:
            raise ValueError(f"raw git diff has an unsupported status: {metadata!r}")
        entries.append((old_mode, new_mode, old_oid, new_oid, status, path))
    return entries


def is_allowed_nvrhi_gitlink_update(entry: tuple[str, str, str, str, str, str]) -> bool:
    old_mode, new_mode, old_oid, new_oid, status, path = entry
    return (
        path == "vendor/nvrhi"
        and old_mode == "160000"
        and new_mode == "160000"
        and status == "M"
        and old_oid != new_oid
    )


def is_protected_change(entry: tuple[str, str, str, str, str, str]) -> bool:
    if is_allowed_nvrhi_gitlink_update(entry):
        return False
    parts = [part.lower() for part in entry[-1].split("/")]
    suffix = "." + parts[-1].rsplit(".", 1)[-1] if "." in parts[-1] else ""
    return bool(PROTECTED_PARTS.intersection(parts) or suffix in PROTECTED_SUFFIXES)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_protected_paths.py <base-sha> <head-sha>", file=sys.stderr)
        return 2

    base, head = sys.argv[1:]
    result = subprocess.run(
        [
            "git",
            "diff",
            "--raw",
            "-z",
            "--no-abbrev",
            "--no-renames",
            "--diff-filter=ACDMRT",
            f"{base}...{head}",
        ],
        capture_output=True,
        check=False,
    )
    if result.returncode:
        print(result.stderr.decode("utf-8", errors="replace").strip() or "git diff failed", file=sys.stderr)
        return result.returncode

    protected: list[str] = []
    try:
        entries = parse_raw_diff_entries(result.stdout)
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 2

    for entry in entries:
        path = entry[-1]
        if is_protected_change(entry):
            protected.append(path)

    if protected:
        print("Pull request modifies protected vendored or generated paths:", file=sys.stderr)
        for path in protected:
            print(f"  {path}", file=sys.stderr)
        return 1

    print("Protected-path check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
