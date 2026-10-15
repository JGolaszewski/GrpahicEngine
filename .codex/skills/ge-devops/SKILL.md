---
name: ge-devops
description: Handle GraphicEngine Git and GitHub operations involving cleanup, branch/worktree administration, larger change integration, GitHub Actions workflows or runs, and pull-request preparation.
---

# GraphicEngine DevOps

Handle safe Git/GitHub operations. This skill carries its own essential repository boundaries so it remains usable in a fresh checkout.

## Task, branch, and review boundaries

- For implementation work, use a repository issue linked to the user-owned **Graphics Engine** project (#1). Search for an existing issue before creating one, define its scope and acceptance criteria, and keep its status current. If project access is unavailable, stop before implementation.
- For one task, branch from `main` as `feat/<issue>-<slug>` or `fix/<issue>-<slug>`. Do not commit or push directly to `main`.
- For at least two independently tracked child tasks or parallel coding, use a parent issue and an `epic/<issue>-<slug>` branch from `main`; task branches start from the epic, their PRs target it, and one integration PR targets `main`. The task coordinator owns issue breakdown, project status, and integration decisions.
- Bring changes into `main` through a pull request. Require human approval and passing required checks. Never approve or self-approve an agent-authored PR targeting `main`; merge only through the approved PR after explicit user authorization.
- Keep unrelated issues in separate commits and PRs. Use the repository PR template when one exists.

## Git cleanup and worktrees

- Inspect `git status --short --branch`, `git worktree list --porcelain`, and candidate branch state before proposing cleanup.
- Show exact paths/branches and likely data loss. Obtain user approval for the specific targets before deleting files, branches, worktrees, or stale metadata. A dry run is not approval.
- Keep each agent/task isolated in its own worktree. Do not switch or remove a worktree if that could overwrite or discard uncommitted work.

## GitHub Actions and pull requests

- Before editing a workflow, inspect its triggers, path filters, permissions, referenced scripts/actions, and secret requirements. Keep changes scoped to the requested automation and repository conventions.
- For failed runs, inspect the run summary and failed-step logs; distinguish code failures from credentials, runner, or external-service issues. Do not dispatch workflows with side effects unless requested.
- Create or update a PR only when requested. Find and follow the repository PR template when present. Apply the task, branch, and review boundaries above.
- Before handoff, report the exact branch/worktrees, changed areas, PR target/state, Actions checks and results, cleanup performed, and unresolved risks.

## Useful inspection commands

```powershell
git status --short --branch
git worktree list --porcelain
git clean -nd
git worktree prune --dry-run
gh workflow list
gh run list --limit 10
```

When GitHub CLI is installed and authenticated, inspect a failed run with `gh run view <run-id> --log-failed`; inspect a PR with `gh pr view <number>`, `gh pr checks <number>`, and `gh pr diff <number>`. Do not run destructive Git commands without the target-specific approval above.
