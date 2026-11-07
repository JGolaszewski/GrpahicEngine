"""Reject pull requests that modify vendored or generated engine outputs."""

from __future__ import annotations

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


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_protected_paths.py <base-sha> <head-sha>", file=sys.stderr)
        return 2

    base, head = sys.argv[1:]
    result = subprocess.run(
        [
            "git",
            "diff",
            "--name-only",
            "--no-renames",
            "--diff-filter=ACDMRT",
            f"{base}...{head}",
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if result.returncode:
        print(result.stderr.strip() or "git diff failed", file=sys.stderr)
        return result.returncode

    protected: list[str] = []
    for value in result.stdout.splitlines():
        path = value.replace("\\", "/")
        parts = [part.lower() for part in path.split("/")]
        suffix = "." + parts[-1].rsplit(".", 1)[-1] if "." in parts[-1] else ""
        if PROTECTED_PARTS.intersection(parts) or suffix in PROTECTED_SUFFIXES:
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
