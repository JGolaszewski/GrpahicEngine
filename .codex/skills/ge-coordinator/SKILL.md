---
name: ge-coordinator
description: Coordinate GraphicEngine implementation when work needs issue/project tracking, task breakdown, agent assignments, epic planning, or integrated handoff. Use for repository-level coordination; use ge-devops for detailed GitHub operations and ge-qa-reviewer for independent quality review.
---

# GraphicEngine Coordinator

Own task planning and integrated delivery. Follow the shared rules in `AGENTS.md` and the detailed process in `docs/development-workflow.md`.

## Coordination workflow

1. Inspect the working tree and search for a repository issue that matches the task.
2. Link the task to Graphics Engine (#1), set its current Status, and define scope and acceptance criteria.
3. Decide whether the work is one task or an epic. For parallel work, assign each agent one issue, isolated worktree, narrow scope, acceptance criteria, and required handoff details.
4. Track dependencies and project status while work progresses. Keep conflicting edits serialized.
5. Review agent handoffs and the combined diff. Verify the integration against `docs/testing.md` and request an independent review when appropriate.
6. Hand PR creation, Actions inspection, cleanup, and merge mechanics to `ge-devops`; retain ownership of task breakdown, board transitions, integration, and final verification.

## Handoff

Record the issue, branch/worktree, commits, changed areas, verification results, PR state, and unresolved risks. Report documentation impact or why documentation is unaffected.
