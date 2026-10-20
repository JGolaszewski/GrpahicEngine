---
name: ge-coordinator
description: Coordinate GraphicEngine implementation when work needs issue/project tracking, task breakdown, agent assignments, epic planning, or integrated handoff.
---

# GraphicEngine Coordinator

Own task planning and integrated delivery. This skill includes the repository's core coordination and review boundaries so it can be used in a fresh checkout.

## Coordination workflow

1. Inspect the working tree and search for a matching repository issue before creating one. Link implementation issues to the user-owned Graphics Engine project (#1). Record an impact map in the issue: owner, affected producers and consumers, interface contracts, documentation, verification, and acceptance criteria. Resolve live project field options, then keep Status current: Backlog before scoping, Ready when scoped, In progress during work, In review while a PR is open, and Done after merge. Check issue state and board Status separately. Stop before implementation if project access is unavailable.
2. For one task, use a `feat/<issue>-<slug>` or `fix/<issue>-<slug>` branch from `main`. For at least two independent child tasks or parallel agents, create a parent issue and `epic/<issue>-<slug>` integration branch from `main`; each child gets an issue and branch from the epic, and its PR targets the epic. Open one integration PR from the epic to `main` after integration.
3. Keep tightly coupled cross-subsystem changes with one coding owner; split independent deliverables into child issues. Assign each coding agent one issue, an isolated worktree, narrow scope, acceptance criteria, and handoff details. When a new consumer or dependency is found, update the issue and assignment before editing it. Serialize conflicting edits.
4. Review each handoff and the combined diff for producer/consumer compatibility, documentation, and test coverage. Verify the integrated branch against `docs/testing.md`; request independent review when useful and report missing checks accurately.
5. Prepare a PR only when requested. Never commit or push directly to `main`. PRs targeting `main` require human approval and passing required checks; an agent must not approve or self-approve them. Merge only through an approved PR after explicit user authorization.

## Handoff

Record the issue, branch/worktree, commits, changed producers and consumers, interface and documentation impact, exact verification results, PR state, and unresolved risks. Confirm board Status at each PR transition, including child merges to an epic and the final merge to `main`. The coordinator owns task breakdown, project transitions, branch integration, and final verification.
