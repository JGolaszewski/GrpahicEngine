---
name: ge-devops
description: Handle GraphicEngine Git and GitHub operations involving cleanup, branch/worktree administration, larger change integration, GitHub Actions workflows or runs, and pull-request preparation. Use for repository DevOps tasks; use ge-coordinator for issue/project ownership and ge-qa-reviewer for independent review.
---

# GraphicEngine DevOps

Handle safe Git/GitHub operations. Follow `AGENTS.md` for shared boundaries and `docs/development-workflow.md` for task branches, epics, project workflow, and PR conventions.

## Git cleanup and worktrees

- Inspect `git status --short --branch`, `git worktree list --porcelain`, and candidate branch state before proposing cleanup.
- Show exact paths/branches and likely data loss. Obtain user approval for the specific targets before deleting files, branches, worktrees, or stale metadata. A dry run is not approval.
- Keep each agent/task isolated in its own worktree. Do not switch or remove a worktree if that could overwrite or discard uncommitted work.

## GitHub Actions and pull requests

- Before editing a workflow, inspect its triggers, path filters, permissions, referenced scripts/actions, and secret requirements. Keep changes scoped to the requested automation and repository conventions.
- For failed runs, inspect the run summary and failed-step logs; distinguish code failures from credentials, runner, or external-service issues. Do not dispatch workflows with side effects unless requested.
- Create or update a PR only when requested. Find and follow the repository PR template. Follow the PR and human-approval boundary in `AGENTS.md`; never submit an agent approval for a PR targeting `main`.
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
