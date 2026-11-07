"""Run the repository agent/skill consistency check after relevant edits."""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
RELEVANT = re.compile(
    r"^(?:\.codex/(?:config\.toml|agents/|hooks/)|\.agents/skills/|"
    r"scripts/check_agentic_setup\.py$|docs/(?:agentic-development|development-workflow|codex-hooks|codex-hook-[^/]+)\.md$)",
    re.IGNORECASE,
)
PATCH_PATH = re.compile(r"(?m)^\*\*\* (?:Update|Add|Delete) File:\s*(.+?)\s*$")


def paths_from_event(event: dict[str, Any]) -> list[str]:
    tool_input = event.get("tool_input")
    paths: list[str] = []
    if isinstance(tool_input, dict):
        for key in ("file_path", "path", "filename"):
            value = tool_input.get(key)
            if isinstance(value, str):
                paths.append(value)
        patch = tool_input.get("command") or tool_input.get("patch")
        if isinstance(patch, str):
            paths.extend(match.group(1).strip() for match in PATCH_PATH.finditer(patch))
    elif isinstance(tool_input, str):
        paths.extend(match.group(1).strip() for match in PATCH_PATH.finditer(tool_input))
    return paths


def main() -> int:
    try:
        event = json.load(sys.stdin)
    except (json.JSONDecodeError, OSError):
        return 0
    if not isinstance(event, dict) or event.get("tool_name") not in {"apply_patch", "Edit", "Write"}:
        return 0
    paths = paths_from_event(event)
    relevant = False
    for value in paths:
        candidate = Path(value)
        if not candidate.is_absolute():
            candidate = ROOT / candidate
        try:
            normalized = candidate.resolve(strict=False).relative_to(ROOT).as_posix()
        except (OSError, ValueError, RuntimeError):
            continue
        if RELEVANT.search(normalized):
            relevant = True
            break
    if not relevant:
        return 0
    checker = ROOT / "scripts" / "check_agentic_setup.py"
    try:
        result = subprocess.run(
            [sys.executable, str(checker)],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=8,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        print(
            json.dumps(
                {
                    "hookSpecificOutput": {
                        "hookEventName": "PostToolUse",
                        "additionalContext": (
                            "Agentic setup consistency check was skipped because the checker "
                            f"could not complete: {error}."
                        ),
                    },
                }
            )
        )
        return 0

    if not result.returncode:
        return 0

    detail = (result.stderr or result.stdout).strip()[-5000:]
    is_validation_failure = "Agentic setup check failed with " in detail
    reason = "Agentic setup consistency check failed." if is_validation_failure else (
        "Agentic setup consistency check could not complete successfully."
    )
    output = {
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": detail or "The checker exited without diagnostic output.",
        }
    }
    if is_validation_failure:
        output["decision"] = "block"
        output["reason"] = reason
    else:
        output["hookSpecificOutput"]["additionalContext"] = reason + " " + output[
            "hookSpecificOutput"
        ]["additionalContext"]
    print(json.dumps(output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
