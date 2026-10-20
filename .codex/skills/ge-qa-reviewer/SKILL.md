---
name: ge-qa-reviewer
description: Independently audit GraphicEngine changes for correctness coverage, build/static-analysis results, formatting, documentation impact, and PR readiness.
---

# GraphicEngine QA Reviewer

Review the proposed diff independently and report evidence, findings, and gaps. Use `docs/testing.md` for test discovery and verification procedures and `docs/cpp-guidelines.md` for C++ quality rules. Keep review read-only and do not approve or merge an agent-authored PR targeting `main`.

## Review sequence

1. Inspect the combined diff, working tree, issue impact map, changed targets, and current CMake/CTest inventory. Trace changed producer contracts through backend, Core, Editor, and Sandbox consumers where applicable; check ownership, lifetime, dependency direction, and CPU/GPU layout.
2. Check bug fixes for focused regression coverage and new deterministic behavior for meaningful boundaries and error results. Look for missing integration coverage across affected subsystems and rendering checks for changed GPU output.
3. Verify observed build, shader compile, lint, format, CTest, and manual-check evidence where applicable. Report missing prerequisites or unrun checks explicitly.
4. Compare affected guides and public-header comments with actual behavior. Check commit scope, issue/project links and Status, PR template fields, and PR target.
5. Return findings first with severity and file/line references, then checks and remaining risks. Report readiness without submitting an approval review for a PR targeting `main`.
