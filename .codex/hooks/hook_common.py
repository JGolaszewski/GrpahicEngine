"""Shared helpers for the repository's Codex hooks."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterable


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
BUILD_DIRECTORY = REPOSITORY_ROOT / "out" / "build" / "x64-debug"
CPP_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inl",
    ".ipp",
    ".ixx",
}
EXCLUDED_DIRECTORY_NAMES = {"vendor", "third_party", "third-party", "out", "build"}
MAX_OUTPUT_CHARACTERS = 5000


def read_event() -> dict[str, Any] | None:
    try:
        stream = getattr(sys.stdin, "buffer", None)
        raw = stream.read().decode("utf-8") if stream is not None else sys.stdin.read()
        value = json.loads(raw)
    except (json.JSONDecodeError, OSError, UnicodeDecodeError):
        return None
    return value if isinstance(value, dict) else None


def hook_input(event: dict[str, Any]) -> dict[str, Any]:
    value = event.get("tool_input")
    return value if isinstance(value, dict) else {}


def normalize_path(value: str) -> Path | None:
    """Normalize tool paths that may use either Windows or POSIX separators."""
    raw = value.strip().strip('"').replace("\\", os.sep).replace("/", os.sep)
    if not raw:
        return None

    candidate = Path(raw)
    if not candidate.is_absolute():
        candidate = REPOSITORY_ROOT / candidate

    try:
        normalized = candidate.resolve(strict=False)
        normalized.relative_to(REPOSITORY_ROOT)
    except (OSError, ValueError, RuntimeError):
        return None

    relative_parts = normalized.relative_to(REPOSITORY_ROOT).parts
    if any(part.casefold() in EXCLUDED_DIRECTORY_NAMES for part in relative_parts[:-1]):
        return None
    if normalized.suffix.casefold() not in CPP_EXTENSIONS:
        return None
    return normalized


def _patch_paths(patch_text: str) -> Iterable[str]:
    patterns = (
        r"(?m)^\*\*\* (?:Update|Add|Delete) File:\s*(.+?)\s*$",
        r"(?m)^\*\*\* Move to:\s*(.+?)\s*$",
    )
    for pattern in patterns:
        for match in re.finditer(pattern, patch_text):
            yield match.group(1).strip().strip('"')

    # Also accept ordinary unified diffs supplied to apply_patch.
    for match in re.finditer(r"(?m)^\+\+\+ (?:b/)?(.+?)\s*$", patch_text):
        value = match.group(1).strip()
        if value != "/dev/null":
            yield value


def touched_cpp_paths(event: dict[str, Any]) -> list[Path]:
    tool_name = event.get("tool_name")
    if tool_name not in {"apply_patch", "Edit", "Write"}:
        return []

    tool_data = hook_input(event)
    candidates: list[str] = []
    for key in ("file_path", "path", "filename"):
        value = tool_data.get(key)
        if isinstance(value, str):
            candidates.append(value)

    if tool_name == "apply_patch":
        if isinstance(event.get("tool_input"), str):
            candidates.extend(_patch_paths(event["tool_input"]))
        for key in ("patch", "command"):
            value = tool_data.get(key)
            if isinstance(value, str):
                candidates.extend(_patch_paths(value))

    result: dict[str, Path] = {}
    for value in candidates:
        path = normalize_path(value)
        if path is not None and path.is_file():
            result[str(path).casefold()] = path
    return list(result.values())


def read_cmake_cache_value(name: str) -> str | None:
    cache_file = BUILD_DIRECTORY / "CMakeCache.txt"
    try:
        lines = cache_file.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return None

    prefix = f"{name}:"
    for line in lines:
        if line.startswith(prefix) and "=" in line:
            return line.split("=", 1)[1].strip()
    return None


def executable_from_cache(cache_name: str, command: str) -> str | None:
    cached = read_cmake_cache_value(cache_name)
    if cached and Path(cached).is_file():
        return cached
    return shutil.which(command)


def ctest_executable(cmake: str) -> str:
    sibling = Path(cmake).with_name("ctest.exe" if os.name == "nt" else "ctest")
    if sibling.is_file():
        return str(sibling)
    return shutil.which("ctest") or "ctest"


def run_command(arguments: list[str], timeout: int | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        arguments,
        cwd=REPOSITORY_ROOT,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        check=False,
    )


def output_tail(result: subprocess.CompletedProcess[str]) -> str:
    output = "\n".join(part for part in (result.stdout, result.stderr) if part)
    return output[-MAX_OUTPUT_CHARACTERS:]


def write_block(reason: str, context: str = "") -> None:
    result: dict[str, Any] = {"decision": "block", "reason": reason}
    if context:
        result["hookSpecificOutput"] = {
            "hookEventName": "PostToolUse",
            "additionalContext": context,
        }
    sys.stdout.write(json.dumps(result, separators=(",", ":")) + "\n")


def event_command(event: dict[str, Any]) -> str:
    value = hook_input(event).get("command", "")
    if isinstance(value, str):
        return value
    if isinstance(value, list):
        return " ".join(str(part) for part in value)
    return ""
