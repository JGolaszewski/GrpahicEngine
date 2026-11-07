"""Narrow GraphicEngine Codex hooks for protected paths and changed-file checks."""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
EDIT_TOOLS = {"apply_patch", "Edit", "Write"}
PATCH_PATH = re.compile(r"(?m)^\*\*\* (?:Update|Add|Delete) File:\s*(.+?)\s*$")
UNIFIED_PATH = re.compile(r"(?m)^\+\+\+ b/(.+?)\s*$")
CPP_EXTENSIONS = {".h", ".hpp", ".cpp", ".cc"}
JSON_EXTENSIONS = {".json", ".gltf"}
SHADER_EXTENSIONS = {".hlsl", ".hlsli"}
FORMAT_VERSION_FILE = ROOT / ".codex" / "hooks" / "clang-format-version.txt"
DEBUG_BUILD = ROOT / "out" / "build" / "x64-debug"


def emit(
    event_name: str,
    context: str,
    *,
    deny: bool = False,
    block_reason: str | None = None,
) -> None:
    if not context and not deny and not block_reason:
        return
    if deny:
        result = {
            "hookSpecificOutput": {
                "hookEventName": event_name,
                "permissionDecision": "deny",
                "permissionDecisionReason": context,
            }
        }
    elif block_reason:
        result = {
            "decision": "block",
            "reason": block_reason,
            "hookSpecificOutput": {
                "hookEventName": event_name,
                "additionalContext": context,
            },
        }
    else:
        result = {
            "hookSpecificOutput": {
                "hookEventName": event_name,
                "additionalContext": context,
            }
        }
    print(json.dumps(result, ensure_ascii=False))


def event_paths(event: dict[str, Any]) -> list[Path]:
    value = event.get("tool_input")
    candidates: list[str] = []
    if isinstance(value, dict):
        for key in ("file_path", "path", "filename"):
            path_value = value.get(key)
            if isinstance(path_value, str):
                candidates.append(path_value)
        command = value.get("command")
        if isinstance(command, str):
            candidates.extend(match.group(1).strip() for match in PATCH_PATH.finditer(command))
            candidates.extend(match.group(1).strip() for match in UNIFIED_PATH.finditer(command))
    elif isinstance(value, str):
        candidates.extend(match.group(1).strip() for match in PATCH_PATH.finditer(value))
        candidates.extend(match.group(1).strip() for match in UNIFIED_PATH.finditer(value))

    resolved: list[Path] = []
    for candidate in candidates:
        path = Path(candidate)
        if not path.is_absolute():
            path = ROOT / path
        try:
            path = path.resolve(strict=False)
            path.relative_to(ROOT)
        except (OSError, RuntimeError, ValueError):
            continue
        if path not in resolved:
            resolved.append(path)
    return resolved


def repo_relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def protected_reason(path: Path) -> str | None:
    relative = repo_relative(path).lower()
    parts = relative.split("/")
    if "vendor" in parts:
        return "Repository policy keeps vendor/ read-only. Do not edit third-party source."
    if any(part in {"out", "build", "bin", "artifacts", "cmakefiles"} for part in parts):
        return "This path is a generated or build output. Edit its first-party source instead."
    if path.suffix.lower() in {".dxbc", ".cso", ".spv", ".rdc", ".obj", ".o", ".pdb", ".ilk", ".lib", ".dll", ".exe"}:
        return "This file is a generated binary or GPU capture. Edit its source instead."
    return None


def pre_tool(event: dict[str, Any]) -> None:
    if event.get("tool_name") not in EDIT_TOOLS:
        return
    for path in event_paths(event):
        reason = protected_reason(path)
        if reason:
            emit("PreToolUse", f"Blocked edit to {repo_relative(path)}. {reason}", deny=True)
            return


def run_command(args: list[str], *, timeout: int = 60) -> tuple[int | None, str]:
    try:
        result = subprocess.run(
            args,
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return None, str(error)
    output = "\n".join(part for part in (result.stdout, result.stderr) if part).strip()
    return result.returncode, output


def format_check(paths: list[Path], notices: list[str], failures: list[str]) -> None:
    if not paths:
        return
    executable = shutil.which("clang-format")
    try:
        expected = FORMAT_VERSION_FILE.read_text(encoding="utf-8").strip()
    except OSError as error:
        notices.append(f"C++ format check skipped: could not read the pinned clang-format version: {error}.")
        return
    if not executable:
        notices.append(f"C++ format check skipped: clang-format {expected} is not installed.")
        return
    version_code, version_output = run_command([executable, "--version"], timeout=5)
    if version_code is None:
        notices.append(f"C++ format check skipped: clang-format could not be run: {version_output}.")
        return
    if version_code or expected not in version_output:
        notices.append(
            f"C++ format check skipped: expected clang-format {expected}; found {version_output or 'unknown version'}."
        )
        return
    code, output = run_command([executable, "--dry-run", "--Werror", *(str(path) for path in paths)], timeout=30)
    if code is None:
        notices.append(f"C++ format check skipped: clang-format could not complete: {output}.")
    elif code:
        failures.append("Changed C++ files fail the read-only clang-format check.\n" + output[-2500:])


def validate_json(paths: list[Path], notices: list[str], failures: list[str]) -> None:
    for path in paths:
        try:
            json.loads(path.read_text(encoding="utf-8-sig"))
        except OSError as error:
            notices.append(f"JSON syntax check skipped for {repo_relative(path)}: could not read file: {error}.")
        except (UnicodeError, json.JSONDecodeError) as error:
            failures.append(f"Invalid JSON input {repo_relative(path)}: {error}")


def compile_shaders(paths: list[Path], notices: list[str], failures: list[str]) -> None:
    if not paths:
        return
    cache = DEBUG_BUILD / "CMakeCache.txt"
    output_dir = DEBUG_BUILD / "bin" / "shaders"
    expected_outputs = list((ROOT / "GraphicEngineBackends" / "D3D11" / "shaders").glob("*_vs.hlsl"))
    expected_outputs += list((ROOT / "GraphicEngineBackends" / "D3D11" / "shaders").glob("*_ps.hlsl"))
    if not cache.is_file() or not expected_outputs or any(
        not (output_dir / f"{source.stem}.dxbc").is_file() for source in expected_outputs
    ):
        notices.append(
            "Shader compilation skipped because the configured x64-debug shader outputs are incomplete. "
            "Run the documented x64-debug configure/build workflow, then rebuild target CompileShaders."
        )
        return
    code, output = run_command(
        ["cmake", "--build", str(DEBUG_BUILD), "--target", "CompileShaders"], timeout=60
    )
    if code is None:
        notices.append(f"Shader compilation skipped because the command could not complete: {output}.")
    elif code:
        failures.append("Changed shader inputs failed the incremental CompileShaders build.\n" + output[-3000:])


def validation_reminder(paths: list[Path]) -> str | None:
    relative = [repo_relative(path).lower() for path in paths]
    rendering = any(
        "/modules/rendering/" in path
        or path.startswith("graphicenginebackends/d3d11/")
        or "/shaders/" in path
        for path in relative
    )
    assets = any("/modules/assets/" in path or "/modules/scene/" in path for path in relative)
    if rendering:
        return (
            "Rendering-related files changed. Use the renderer/shader validation skills as applicable; "
            "for GPU or lifetime changes follow docs/validation/windows.md. "
            "This hook has not run those checks or verified visual output."
        )
    if assets:
        return (
            "Asset or scene files changed. Select the focused tests from docs/testing.md; "
            "JSON syntax validation does not establish schema or runtime import correctness."
        )
    return None


def post_edit(event: dict[str, Any]) -> None:
    if event.get("tool_name") not in EDIT_TOOLS:
        return
    paths = [path for path in event_paths(event) if protected_reason(path) is None and path.is_file()]
    if not paths:
        return

    notices: list[str] = []
    failures: list[str] = []
    first_party = [
        path
        for path in paths
        if path.suffix.lower() in CPP_EXTENSIONS
        and any(
            repo_relative(path).lower().startswith(prefix)
            for prefix in (
                "graphicenginecore/",
                "graphicengineeditor/",
                "graphicenginebackends/",
                "sandboxapp/",
            )
        )
    ]
    json_inputs = [path for path in paths if path.suffix.lower() in JSON_EXTENSIONS]
    shader_inputs = [
        path
        for path in paths
        if path.suffix.lower() in SHADER_EXTENSIONS
        and repo_relative(path).lower().startswith("graphicenginebackends/d3d11/shaders/")
    ]

    format_check(first_party, notices, failures)
    validate_json(json_inputs, notices, failures)
    compile_shaders(shader_inputs, notices, failures)
    reminder = validation_reminder(paths)
    if reminder:
        notices.append(reminder)
    if failures:
        notices.insert(0, "Changed-file validation failed:\n" + "\n\n".join(failures))
    emit(
        "PostToolUse",
        "\n\n".join(notices),
        block_reason="Changed-file validation failed." if failures else None,
    )


def text_values(value: Any) -> list[str]:
    values: list[str] = []
    if isinstance(value, str):
        values.append(value)
    elif isinstance(value, dict):
        for child in value.values():
            values.extend(text_values(child))
    elif isinstance(value, list):
        for child in value:
            values.extend(text_values(child))
    return values


def gpu_report(event: dict[str, Any]) -> None:
    if event.get("tool_name") != "Bash":
        return
    output = "\n".join(text_values(event.get("tool_response")))
    match = re.search(r"GRAPHICENGINE_GPU_REPORT:(.+)", output)
    if not match:
        return
    report_path = Path(match.group(1).strip().strip('"'))
    if not report_path.is_absolute():
        report_path = ROOT / report_path
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        emit("PostToolUse", f"GPU diagnostic smoke report could not be read: {error}")
        return

    if not isinstance(report, dict):
        emit("PostToolUse", "GPU diagnostic smoke report was not a JSON object; no validation conclusion was made.")
        return

    status = report.get("status", "unknown")
    errors = report.get("nvrhi_errors", "unknown")
    warnings = report.get("nvrhi_warnings", "unknown")
    exit_code = report.get("editor_exit_code", "unknown")
    errors_value = report.get("nvrhi_errors")
    try:
        has_logged_errors = int(errors_value) > 0
    except (TypeError, ValueError):
        has_logged_errors = False
    failed_status = isinstance(status, str) and status in {
        "launch_or_log_error",
        "no_log_evidence",
        "editor_exit_nonzero",
        "validation_errors",
    }
    try:
        editor_failed = int(exit_code) != 0
    except (TypeError, ValueError):
        editor_failed = False
    emit(
        "PostToolUse",
        "GPU diagnostic session result: "
        f"status={status}, NVRHI errors={errors}, warnings={warnings}, Editor exit={exit_code}. "
        "This reports logged diagnostics only; it does not verify that visual scenarios were exercised.",
        block_reason="GPU diagnostic session reported a validation failure."
        if failed_status or has_logged_errors or editor_failed
        else None,
    )


def main() -> int:
    try:
        event = json.load(sys.stdin)
    except (json.JSONDecodeError, OSError):
        return 0
    if not isinstance(event, dict):
        return 0

    event_name = event.get("hook_event_name")
    if event_name == "PreToolUse":
        pre_tool(event)
    elif event_name == "PostToolUse":
        if event.get("tool_name") in EDIT_TOOLS:
            post_edit(event)
        elif event.get("tool_name") == "Bash":
            gpu_report(event)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
