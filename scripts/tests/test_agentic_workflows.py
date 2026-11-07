"""Regression tests for project-local Codex hooks and PR handoff validation."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


graphics_hooks = load_module("graphics_hooks", ROOT / ".codex/hooks/graphics_hooks.py")
agentic_setup_hook = load_module(
    "agentic_setup_hook", ROOT / ".codex/hooks/agentic_setup_check.py"
)
pr_body_checker = load_module("check_pr_body", ROOT / ".github/scripts/check_pr_body.py")


def invoke_main(module, payload: str) -> tuple[int, str]:
    output = io.StringIO()
    with mock.patch.object(module.sys, "stdin", io.StringIO(payload)):
        with contextlib.redirect_stdout(output):
            result = module.main()
    return result, output.getvalue()


def handoff_body(
    *, issue: str, project: str, lightweight_reason: str = "", acceptance_criteria: str = ""
) -> str:
    tracking = f"Issue: {issue}\n"
    if lightweight_reason:
        tracking += f"Lightweight reason: {lightweight_reason}\n"
    tracking += f"Project: {project}"
    return f"""## Tracking
{tracking}

## Impact
Changed subsystems: docs
Producer/consumer contracts: None; documentation-only change.
{f"Acceptance criteria: {acceptance_criteria}" if acceptance_criteria else ""}

## Documentation
Updated: docs/example.md

## Verification
- PASS: `python -m unittest discover -s scripts/tests -v`

## Risks
None identified.
"""


class GraphicsHookTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.root_patch = mock.patch.object(graphics_hooks, "ROOT", self.root)
        self.root_patch.start()
        self.format_patch = mock.patch.object(
            graphics_hooks, "FORMAT_VERSION_FILE", self.root / ".codex/hooks/clang-format-version.txt"
        )
        self.format_patch.start()
        self.build_patch = mock.patch.object(
            graphics_hooks, "DEBUG_BUILD", self.root / "out/build/x64-debug"
        )
        self.build_patch.start()
        graphics_hooks.FORMAT_VERSION_FILE.parent.mkdir(parents=True, exist_ok=True)
        graphics_hooks.FORMAT_VERSION_FILE.write_text("18.1.8", encoding="utf-8")

    def tearDown(self) -> None:
        self.build_patch.stop()
        self.format_patch.stop()
        self.root_patch.stop()
        self.temp.cleanup()

    def test_pre_tool_denies_vendored_path(self) -> None:
        event = {
            "hook_event_name": "PreToolUse",
            "tool_name": "Write",
            "tool_input": {"file_path": "vendor/third_party/library.h"},
        }
        code, output = invoke_main(graphics_hooks, json.dumps(event))

        self.assertEqual(code, 0)
        result = json.loads(output)
        self.assertEqual(result["hookSpecificOutput"]["hookEventName"], "PreToolUse")
        self.assertEqual(result["hookSpecificOutput"]["permissionDecision"], "deny")
        self.assertIn("vendor/third_party/library.h", result["hookSpecificOutput"]["permissionDecisionReason"])

    def test_pre_tool_denies_generated_binary_but_allows_source(self) -> None:
        blocked = {"tool_name": "Edit", "tool_input": {"path": "artifacts/shader.spv"}}
        allowed = {"tool_name": "Edit", "tool_input": {"path": "GraphicEngineCore/main.cpp"}}

        with contextlib.redirect_stdout(io.StringIO()) as output:
            graphics_hooks.pre_tool(blocked)
        self.assertIn("permissionDecision", json.loads(output.getvalue())["hookSpecificOutput"])

        with contextlib.redirect_stdout(io.StringIO()) as output:
            graphics_hooks.pre_tool(allowed)
        self.assertEqual(output.getvalue(), "")

    def test_pre_tool_ignores_non_edit_tool(self) -> None:
        event = {"tool_name": "Bash", "tool_input": {"path": "vendor/library.h"}}
        with contextlib.redirect_stdout(io.StringIO()) as output:
            graphics_hooks.pre_tool(event)
        self.assertEqual(output.getvalue(), "")

    def test_event_paths_parse_patch_and_ignore_outside_root(self) -> None:
        patch_event = {
            "tool_input": {
                "command": "*** Update File: docs/agentic-development.md\n"
                "+++ b/AGENTS.md\n"
                "+++ b/../../outside.txt\n"
            }
        }

        paths = graphics_hooks.event_paths(patch_event)

        self.assertEqual(
            paths,
            [self.root / "docs/agentic-development.md", self.root / "AGENTS.md"],
        )

    def test_post_edit_reports_invalid_json_and_accepts_valid_json(self) -> None:
        asset = self.root / "GraphicEngineCore/modules/scene/example.json"
        asset.parent.mkdir(parents=True)
        asset.write_text("{broken", encoding="utf-8")
        event = {"hook_event_name": "PostToolUse", "tool_name": "Write", "tool_input": {"path": str(asset)}}

        _, output = invoke_main(graphics_hooks, json.dumps(event))
        result = json.loads(output)
        self.assertEqual(result["decision"], "block")
        self.assertEqual(result["reason"], "Changed-file validation failed.")
        self.assertIn("Invalid JSON input", result["hookSpecificOutput"]["additionalContext"])

        asset.write_text('{"ok": true}', encoding="utf-8")
        _, output = invoke_main(graphics_hooks, json.dumps(event))
        result = json.loads(output)
        self.assertNotIn("decision", result)
        context = result["hookSpecificOutput"]["additionalContext"]
        self.assertNotIn("Invalid JSON input", context)
        self.assertIn("JSON syntax validation does not establish schema", context)

    def test_post_edit_cpp_format_failure_is_reported_without_rewriting(self) -> None:
        source = self.root / "GraphicEngineCore/modules/core/example.cpp"
        source.parent.mkdir(parents=True)
        source.write_text("int value;\n", encoding="utf-8")
        event = {"tool_name": "Write", "tool_input": {"file_path": str(source)}}
        command_results = [(0, "clang-format version 18.1.8"), (1, "format mismatch")]

        with mock.patch.object(graphics_hooks.shutil, "which", return_value="clang-format"):
            with mock.patch.object(graphics_hooks, "run_command", side_effect=command_results) as run:
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    graphics_hooks.post_edit(event)

        self.assertEqual(source.read_text(encoding="utf-8"), "int value;\n")
        self.assertIn("--dry-run", run.call_args_list[1].args[0])
        self.assertIn("--Werror", run.call_args_list[1].args[0])
        result = json.loads(output.getvalue())
        self.assertEqual(result["decision"], "block")
        self.assertIn("fail the read-only clang-format check", result["hookSpecificOutput"]["additionalContext"])

    def test_formatter_timeout_is_reported_without_blocking(self) -> None:
        source = self.root / "GraphicEngineCore/modules/core/example.cpp"
        source.parent.mkdir(parents=True)
        source.write_text("int value;\n", encoding="utf-8")
        event = {"tool_name": "Write", "tool_input": {"file_path": str(source)}}

        with mock.patch.object(graphics_hooks.shutil, "which", return_value="clang-format"):
            with mock.patch.object(
                graphics_hooks,
                "run_command",
                side_effect=[(0, "clang-format version 18.1.8"), (None, "timed out")],
            ):
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    graphics_hooks.post_edit(event)

        result = json.loads(output.getvalue())
        self.assertNotIn("decision", result)
        self.assertIn("format check skipped", result["hookSpecificOutput"]["additionalContext"])

    def test_shader_compiler_failure_blocks_but_unavailable_command_does_not(self) -> None:
        shader = self.root / "GraphicEngineBackends/D3D11/shaders/example_vs.hlsl"
        shader.parent.mkdir(parents=True)
        shader.write_text("// test shader\n", encoding="utf-8")
        shader_output = graphics_hooks.DEBUG_BUILD / "bin/shaders/example_vs.dxbc"
        shader_output.parent.mkdir(parents=True)
        shader_output.write_bytes(b"compiled")
        (graphics_hooks.DEBUG_BUILD / "CMakeCache.txt").write_text("cache", encoding="utf-8")
        event = {"tool_name": "Write", "tool_input": {"path": str(shader)}}

        for command_result, should_block in (((1, "shader syntax error"), True), ((None, "cmake missing"), False)):
            with mock.patch.object(graphics_hooks, "run_command", return_value=command_result):
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    graphics_hooks.post_edit(event)
            result = json.loads(output.getvalue())
            self.assertEqual("decision" in result, should_block)

    def test_unreadable_json_is_a_nonblocking_skip(self) -> None:
        path = self.root / "GraphicEngineCore/modules/scene/missing.json"
        with mock.patch.object(Path, "read_text", side_effect=OSError("access denied")):
            notices: list[str] = []
            failures: list[str] = []
            graphics_hooks.validate_json([path], notices, failures)

        self.assertEqual(failures, [])
        self.assertIn("JSON syntax check skipped", notices[0])

    def test_post_edit_shader_skips_when_build_outputs_are_missing(self) -> None:
        shader = self.root / "GraphicEngineBackends/D3D11/shaders/example_vs.hlsl"
        shader.parent.mkdir(parents=True)
        shader.write_text("// test shader\n", encoding="utf-8")
        event = {"tool_name": "Write", "tool_input": {"path": str(shader)}}

        with contextlib.redirect_stdout(io.StringIO()) as output:
            graphics_hooks.post_edit(event)

        self.assertIn("Shader compilation skipped", output.getvalue())

    def test_gpu_report_summarizes_report_and_marks_visual_limit(self) -> None:
        report = self.root / "gpu-report.json"
        report.write_text(
            json.dumps({"status": "completed", "nvrhi_errors": 0, "nvrhi_warnings": 2, "editor_exit_code": 0}),
            encoding="utf-8",
        )
        event = {"tool_name": "Bash", "tool_response": f"GRAPHICENGINE_GPU_REPORT:{report}"}

        with contextlib.redirect_stdout(io.StringIO()) as output:
            graphics_hooks.gpu_report(event)

        result = json.loads(output.getvalue())
        self.assertNotIn("decision", result)
        context = result["hookSpecificOutput"]["additionalContext"]
        self.assertIn("NVRHI errors=0, warnings=2", context)
        self.assertIn("does not verify that visual scenarios were exercised", context)

        report.write_text(
            json.dumps({"status": "validation_errors", "nvrhi_errors": 1, "nvrhi_warnings": 0, "editor_exit_code": 0}),
            encoding="utf-8",
        )
        with contextlib.redirect_stdout(io.StringIO()) as output:
            graphics_hooks.gpu_report(event)
        self.assertEqual(json.loads(output.getvalue())["decision"], "block")

        report.write_text("[]", encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()) as output:
            graphics_hooks.gpu_report(event)
        malformed = json.loads(output.getvalue())
        self.assertNotIn("decision", malformed)
        self.assertIn("not a JSON object", malformed["hookSpecificOutput"]["additionalContext"])

    def test_malformed_hook_input_fails_open_without_output(self) -> None:
        code, output = invoke_main(graphics_hooks, "not-json")
        self.assertEqual(code, 0)
        self.assertEqual(output, "")


class HookRegistrationTests(unittest.TestCase):
    def test_registered_hook_commands_exist_and_have_timeouts(self) -> None:
        with (ROOT / ".codex/config.toml").open("rb") as stream:
            config = tomllib.load(stream)

        hooks = config["hooks"]
        registrations = [
            (event_name, handler)
            for event_name, groups in hooks.items()
            for group in groups
            for handler in group["hooks"]
        ]
        commands = [handler["command"] for _, handler in registrations]

        self.assertTrue(any(".codex/hooks/graphics_hooks.py" in command for command in commands))
        self.assertTrue(any(".codex/hooks/agentic_setup_check.py" in command for command in commands))
        for _, handler in registrations:
            self.assertGreater(handler["timeout"], 0)
            self.assertIn(".codex/hooks/", handler["command"])


class AgenticSetupHookTests(unittest.TestCase):
    def test_relevant_edit_runs_checker(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            event = {
                "tool_name": "Edit",
                "tool_input": {"file_path": "docs/agentic-development.md"},
            }
            result = SimpleNamespace(returncode=0, stdout="passed", stderr="")
            with mock.patch.object(agentic_setup_hook, "ROOT", root):
                with mock.patch.object(agentic_setup_hook.subprocess, "run", return_value=result) as run:
                    code, output = invoke_main(agentic_setup_hook, json.dumps(event))

        self.assertEqual(code, 0)
        self.assertEqual(output, "")
        self.assertEqual(run.call_args.args[0][-1], str(root / "scripts/check_agentic_setup.py"))

    def test_checker_failure_is_returned_as_post_edit_feedback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            event = {
                "tool_name": "Write",
                "tool_input": {"file_path": ".codex/config.toml"},
            }
            result = SimpleNamespace(
                returncode=1,
                stdout="",
                stderr="ERROR: invalid setup\nAgentic setup check failed with 1 issue(s).",
            )
            with mock.patch.object(agentic_setup_hook, "ROOT", root):
                with mock.patch.object(agentic_setup_hook.subprocess, "run", return_value=result):
                    _, output = invoke_main(agentic_setup_hook, json.dumps(event))

        feedback = json.loads(output)
        self.assertEqual(feedback["decision"], "block")
        self.assertIn("invalid setup", feedback["hookSpecificOutput"]["additionalContext"])

    def test_checker_operational_error_is_nonblocking_feedback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            event = {"tool_name": "Write", "tool_input": {"file_path": ".codex/config.toml"}}
            with mock.patch.object(agentic_setup_hook, "ROOT", root):
                with mock.patch.object(agentic_setup_hook.subprocess, "run", side_effect=OSError("python missing")):
                    _, output = invoke_main(agentic_setup_hook, json.dumps(event))

        feedback = json.loads(output)
        self.assertNotIn("decision", feedback)
        self.assertIn("check was skipped", feedback["hookSpecificOutput"]["additionalContext"])

    def test_irrelevant_edit_does_not_run_checker(self) -> None:
        event = {"tool_name": "Write", "tool_input": {"file_path": "README.md"}}
        with mock.patch.object(agentic_setup_hook.subprocess, "run") as run:
            _, output = invoke_main(agentic_setup_hook, json.dumps(event))
        run.assert_not_called()
        self.assertEqual(output, "")


class PullRequestMetadataTests(unittest.TestCase):
    def invoke_checker(self, body: str) -> tuple[int, str, str]:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with mock.patch.dict("os.environ", {"PR_BODY": body}):
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                result = pr_body_checker.main()
        return result, stdout.getvalue(), stderr.getvalue()

    def test_tracked_pr_requires_project_link(self) -> None:
        body = handoff_body(issue="#27", project=pr_body_checker.PROJECT_URL)
        code, output, errors = self.invoke_checker(body)
        self.assertEqual(code, 0, errors)
        self.assertIn("handoff fields are present", output)

    def test_lightweight_pr_accepts_reason_without_project(self) -> None:
        body = handoff_body(
            issue="None",
            project="Not applicable (lightweight change)",
            lightweight_reason="Correct a typo in the internal setup guide.",
            acceptance_criteria="The guide has the corrected spelling.",
        )
        code, _, errors = self.invoke_checker(body)
        self.assertEqual(code, 0, errors)

    def test_lightweight_pr_requires_reason_and_explicit_project_exception(self) -> None:
        body = handoff_body(issue="None", project=pr_body_checker.PROJECT_URL)
        code, _, errors = self.invoke_checker(body)
        self.assertEqual(code, 1)
        self.assertIn("Lightweight reason", errors)
        self.assertIn("Project: Not applicable", errors)

    def test_lightweight_pr_requires_acceptance_criteria(self) -> None:
        body = handoff_body(
            issue="None",
            project="Not applicable (lightweight change)",
            lightweight_reason="Correct an internal documentation typo.",
        )
        code, _, errors = self.invoke_checker(body)
        self.assertEqual(code, 1)
        self.assertIn("Acceptance criteria", errors)

    def test_tracked_pr_without_project_link_is_rejected(self) -> None:
        body = handoff_body(issue="#27", project="Not applicable (lightweight change)")
        code, _, errors = self.invoke_checker(body)
        self.assertEqual(code, 1)
        self.assertIn("Graphics Engine project link", errors)


if __name__ == "__main__":
    unittest.main()
