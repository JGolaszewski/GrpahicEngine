# Development workflow

This page is the detailed procedure for tracking, dividing, and integrating repository work. The root `AGENTS.md` contains the always-on agent contract.

## Track work on GitHub Projects

Every implementation task needs a repository issue linked to the [Graphics Engine project board](https://github.com/users/JGolaszewski/projects/1). Search for an existing issue before creating one; do not use a project-only draft item as the task record.

| Status | When to use it |
| --- | --- |
| Backlog | Accepted task that has not been scoped or started |
| Ready | Scope and acceptance criteria are clear |
| In progress | Implementation or investigation is active |
| In review | A pull request is open for review |
| Done | The change has merged to its target branch |

If project access is unavailable, stop before implementation and report the blocker.

## Branches, commits, and pull requests

For one issue, branch from `main` using `feat/<issue>-<slug>` or `fix/<issue>-<slug>`. Make at least one scoped Conventional Commit per task and include its issue reference, for example:

```text
feat(rendering): add shadow filtering (#123)
```

For at least two independently tracked child tasks, or work requiring parallel agents, create a parent issue and an `epic/<issue>-<slug>` integration branch from `main`. Give each child its own issue and branch from the epic branch; target child PRs to the epic. Integrate the child work there, preserve distinct feature commits, and open one integration PR to `main`.

Follow the PR, human-review, and merge boundary in `AGENTS.md`. Before creating a PR, search for a repository PR template and use it. Keep single-task status `In review` while its PR is open and mark it `Done` after merge. For grouped work, child issues may be completed after their reviewed PRs merge to the epic branch; keep the parent active until the integration PR merges to `main`.

## Parallel agents and handoffs

The coordinating agent owns issue breakdown, board updates, integration, and final verification. Assign each coding agent one issue, a narrow file/module scope, and an isolated worktree/branch. If worktree isolation is unavailable, parallelize read-only research/review or non-overlapping edits and serialize conflicting changes.

Each handoff reports its issue and branch, commits, changed areas, verification commands and results, and unresolved risks. The coordinator reviews the combined change and verifies it before opening or updating the PR.

## Documentation and verification references

Update relevant docs and public-header API comments when behavior, public APIs, architecture, or developer workflow changes. Add new pages to `mkdocs.yml` navigation. If no docs are affected, state that in the PR summary instead of adding artificial documentation.

The documentation workflow runs `scripts/build-docs.ps1` on relevant pull
requests. It lints source Markdown, checks the whole-engine API with warning-as-error
Doxygen settings, and builds the MkDocs Material site from Doxygen XML. A
successful push to `main` deploys the built site through the GitHub Pages
Actions artifact workflow; pull requests validate without deploying.

Use [Testing and performance](testing.md) for test discovery, focused and full CTest runs, build/lint requirements, and performance checks. Do not duplicate its test procedures here.
