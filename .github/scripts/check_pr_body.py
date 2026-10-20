"""Check that a pull request contains the required workflow handoff fields."""

from __future__ import annotations

import os
import re
import sys


HEADINGS = ("Tracking", "Impact", "Documentation", "Verification", "Risks")
PROJECT_URL = "https://github.com/users/JGolaszewski/projects/1"


def sections_from(body: str) -> dict[str, str]:
    visible = re.sub(r"<!--.*?-->", "", body, flags=re.DOTALL)
    headings = list(re.finditer(r"(?m)^## ([^\r\n]+)[ \t]*$", visible))
    sections = {}
    for index, match in enumerate(headings):
        end = headings[index + 1].start() if index + 1 < len(headings) else len(visible)
        sections[match.group(1).strip()] = visible[match.end() : end].strip()
    return sections


def labelled_value(section: str, label: str) -> str:
    match = re.search(rf"(?m)^{re.escape(label)}:[ \t]*([^\r\n]+)$", section)
    return match.group(1).strip() if match else ""


def main() -> int:
    sections = sections_from(os.environ.get("PR_BODY", ""))
    errors = [f"Missing ## {heading} section." for heading in HEADINGS if heading not in sections]

    tracking = sections.get("Tracking", "")
    if not re.search(r"(?m)^Issue:[ \t]*#\d+[ \t]*$", tracking):
        errors.append("Tracking needs an Issue: #number line.")
    if labelled_value(tracking, "Project") != PROJECT_URL:
        errors.append("Tracking needs the Graphics Engine project link.")

    impact = sections.get("Impact", "")
    for label in ("Changed subsystems", "Producer/consumer contracts"):
        if not labelled_value(impact, label):
            errors.append(f"Impact needs a {label}: value.")

    documentation = sections.get("Documentation", "")
    if not (labelled_value(documentation, "Updated") or labelled_value(documentation, "None")):
        errors.append("Documentation needs Updated: links or None: reason.")

    verification = sections.get("Verification", "")
    if not re.search(r"(?m)^- (?:PASS|FAIL|BLOCKED|NOT RUN):[ \t]*\S.+$", verification):
        errors.append("Verification needs a result and an exact command or manual check.")

    if not sections.get("Risks"):
        errors.append("Risks needs a statement, such as None identified.")

    if errors:
        for error in errors:
            print(f"::error::{error}", file=sys.stderr)
        return 1

    print("PR handoff fields are present; reviewers must check their accuracy.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
