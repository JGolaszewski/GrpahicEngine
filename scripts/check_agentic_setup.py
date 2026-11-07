#!/usr/bin/env python3
"""Validate local Codex agent and skill metadata without external dependencies."""

from __future__ import annotations

import re
import sys
import tomllib
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
AGENT_DIR = ROOT / ".codex" / "agents"
SKILL_DIR = ROOT / ".agents" / "skills"
ALLOWED_EFFORTS = {"low", "medium", "high"}
MARKDOWN_LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)]+)\)")


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def read_toml(path: Path, errors: list[str]) -> dict[str, Any] | None:
    try:
        with path.open("rb") as stream:
            value = tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as error:
        fail(errors, f"{path.relative_to(ROOT)}: TOML parse failed: {error}")
        return None
    return value


def check_agent_config(errors: list[str]) -> int:
    config_path = ROOT / ".codex" / "config.toml"
    config = read_toml(config_path, errors)
    if config is None:
        return 0

    registry = config.get("agents", {})
    if not isinstance(registry, dict):
        fail(errors, ".codex/config.toml: [agents] must be a table")
        return 0
    configured = {
        name: value
        for name, value in registry.items()
        if isinstance(value, dict) and "config_file" in value
    }

    files = sorted(AGENT_DIR.glob("*.toml"))
    file_names: set[str] = set()
    for path in files:
        role = read_toml(path, errors)
        if role is None:
            continue
        name = role.get("name")
        if not isinstance(name, str) or not name:
            fail(errors, f"{path.relative_to(ROOT)}: missing non-empty name")
            continue
        if name in file_names:
            fail(errors, f"{path.relative_to(ROOT)}: duplicate agent name {name!r}")
        file_names.add(name)
        for key in ("description", "developer_instructions", "model", "nickname_candidates"):
            if key not in role:
                fail(errors, f"{path.relative_to(ROOT)}: missing {key}")
        effort = role.get("model_reasoning_effort")
        if effort not in ALLOWED_EFFORTS:
            fail(errors, f"{path.relative_to(ROOT)}: effort must be one of {sorted(ALLOWED_EFFORTS)}")
        nicknames = role.get("nickname_candidates")
        if not isinstance(nicknames, list) or not nicknames or any(not isinstance(item, str) or not item for item in nicknames):
            fail(errors, f"{path.relative_to(ROOT)}: nickname_candidates must be a non-empty string array")
        registration = configured.get(name)
        if registration is None:
            fail(errors, f"{path.relative_to(ROOT)}: role {name!r} is not registered in .codex/config.toml")
        else:
            expected_path = f"agents/{path.name}"
            if registration.get("config_file") != expected_path:
                fail(errors, f".codex/config.toml: {name!r} config_file must be {expected_path!r}")

    for name in configured:
        if name not in file_names:
            fail(errors, f".codex/config.toml: registered agent {name!r} has no role file")
    return len(files)


def check_skills(errors: list[str]) -> int:
    names: set[str] = set()
    files = sorted(SKILL_DIR.glob("*/SKILL.md"))
    for path in files:
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        if not lines or lines[0] != "---":
            fail(errors, f"{path.relative_to(ROOT)}: missing YAML frontmatter")
            continue
        try:
            end = lines.index("---", 1)
        except ValueError:
            fail(errors, f"{path.relative_to(ROOT)}: unterminated YAML frontmatter")
            continue
        header = "\n".join(lines[1:end])
        name_match = re.search(r"(?m)^name:\s*([^\s#]+)\s*$", header)
        description_match = re.search(r"(?m)^description:\s*.+\S\s*$", header)
        if not name_match:
            fail(errors, f"{path.relative_to(ROOT)}: missing skill name")
            continue
        name = name_match.group(1).strip("\"'")
        if name in names:
            fail(errors, f"{path.relative_to(ROOT)}: duplicate skill name {name!r}")
        names.add(name)
        if name != path.parent.name:
            fail(errors, f"{path.relative_to(ROOT)}: frontmatter name must match directory {path.parent.name!r}")
        if not description_match:
            fail(errors, f"{path.relative_to(ROOT)}: missing skill description")
    return len(files)


def check_markdown_links(errors: list[str]) -> int:
    candidates = list(SKILL_DIR.glob("*/SKILL.md")) + [
        ROOT / "docs" / "agentic-development.md",
        ROOT / "docs" / "development-workflow.md",
        ROOT / "docs" / "codex-hooks.md",
    ]
    checked = 0
    for path in candidates:
        if not path.is_file():
            continue
        checked += 1
        for match in MARKDOWN_LINK.finditer(path.read_text(encoding="utf-8")):
            target = match.group(1).strip().split(" ", 1)[0].strip("<>")
            if not target or target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            local, _, anchor = target.partition("#")
            if not local:
                continue
            resolved = (path.parent / local).resolve()
            try:
                resolved.relative_to(ROOT)
            except ValueError:
                fail(errors, f"{path.relative_to(ROOT)}: link escapes repository: {target}")
                continue
            if not resolved.exists():
                fail(errors, f"{path.relative_to(ROOT)}: broken link: {target}")
    return checked


def main() -> int:
    errors: list[str] = []
    agent_count = check_agent_config(errors)
    skill_count = check_skills(errors)
    link_count = check_markdown_links(errors)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print(f"Agentic setup check failed with {len(errors)} issue(s).", file=sys.stderr)
        return 1
    print(f"Agentic setup check passed: {agent_count} agents, {skill_count} skills, {link_count} Markdown files checked.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
