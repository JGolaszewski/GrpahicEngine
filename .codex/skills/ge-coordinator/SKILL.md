---
name: ge-coordinator
description: Coordinate GraphicEngine implementation when work needs issue/project tracking, task breakdown, agent assignments, epic planning, or integrated handoff.
---

# GraphicEngine Coordinator

Own task planning and integrated delivery. This skill includes the repository's core coordination and review boundaries so it can be used in a fresh checkout.

## Coordination workflow

1. Inspect the working tree and search for a matching repository issue before creating one. Link implementation issues to the user-owned Graphics Engine project (#1), define scope and acceptance criteria, and keep Status current: Backlog before work, Ready when scoped, In progress during implementation, In review while a PR is open, and Done after merge. Stop before implementation if project access is unavailable.
2. For one task, use a `feat/<issue>-<slug>` or `fix/<issue>-<slug>` branch from `main`. For at least two independent child tasks or parallel agents, create a parent issue and `epic/<issue>-<slug>` integration branch from `main`; each child gets an issue and branch from the epic, and its PR targets the epic. Open one integration PR from the epic to `main` after integration.
3. Assign each agent one issue, an isolated worktree, narrow file/module scope, acceptance criteria, and handoff details. Track dependencies and project status; serialize conflicting edits.
4. Review each handoff and the combined diff. Verify the integration against `docs/testing.md`; request independent review when useful and report missing checks accurately.
5. Prepare a PR only when requested. Never commit or push directly to `main`. PRs targeting `main` require human approval and passing required checks; an agent must not approve or self-approve them. Merge only through an approved PR after explicit user authorization.

## Handoff

Record the issue, branch/worktree, commits, changed areas, verification results, PR state, and unresolved risks. Report documentation impact or why documentation is unaffected. The coordinator owns task breakdown, project transitions, branch integration, and final verification.
