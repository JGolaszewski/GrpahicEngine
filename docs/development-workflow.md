# Development workflow

This page describes how contributors and Codex agents track, implement, review,
and verify engine changes. The root `AGENTS.md` contains the short operational
rules Codex must follow.

## Track work on GitHub Projects

Every implementation task uses a repository issue linked to the
[Graphics Engine project board](https://github.com/users/JGolaszewski/projects/1).
Check for an existing issue before creating a duplicate. Keep its Project status
current:

| Status | When to use it |
| --- | --- |
| Backlog | Accepted task that has not been scoped or started |
| Ready | Scope and acceptance criteria are clear |
| In progress | Implementation or investigation is active |
| In review | A pull request is open for review |
| Done | The change has merged to its target branch |

If GitHub Project access is unavailable, stop before implementation and report
the blocker. Do not silently proceed with untracked work.

## Branches, commits, and pull requests

For one issue, branch from `main` as `feat/<issue>-<slug>` or
`fix/<issue>-<slug>`. Include at least one scoped Conventional Commit for each
feature/task and reference its issue, for example:

```text
feat(rendering): add shadow filtering (#123)
```

For work with at least two independently tracked child tasks, or work that needs
parallel coding agents, create a parent issue and an `epic/<issue>-<slug>` branch
from `main`. Each child task gets its own issue, branch from the epic branch, and
PR targeting that branch. Integrate child work there, then open one PR from the
epic branch to `main`. Preserve distinct feature commits during integration.

Move a single task to In review when its PR opens and to Done after it merges.
For grouped work, child issues can move to Done after their reviewed PRs merge to
the epic branch; keep the parent active until the integration PR merges to
`main`.

## Parallel Codex agents

The coordinating agent owns issue breakdown, board updates, branch integration,
and final verification. Assign each coding agent one issue, a narrow file/module
scope, and an isolated worktree/branch. Agents should not edit the same files at
the same time. If worktree isolation is unavailable, use parallel agents for
research, review, or non-overlapping edits and serialize conflicting changes.

Each handoff reports the issue and branch, commits made, changed areas, commands
and results for build/lint/tests, and any unresolved risks. The coordinator
reviews and verifies the combined change before opening or updating the PR.

## Documentation

Update the relevant docs and public-header API comments when a change affects
documented behavior, public APIs, architecture, or developer workflow. Add new
pages to `mkdocs.yml` navigation. If no docs are affected, state that in the PR
summary; do not add artificial documentation changes.

## Tests and clang-tidy

Before adding or changing tests, inspect the current checkout's CMake test
registration and `ctest -N` output. They define the available targets, labels,
and test framework; older docs or another branch may be stale.

Add regression coverage for bug fixes and focused tests for new deterministic
behavior, important edge cases, and failure results. Keep unit tests fast and
independent of windows, GPU devices, services, and unnecessary filesystem state.
Use integration tests when real subsystem or platform behavior is required.

Configure and build the affected target, run the focused CTest selection, and run
the full registered suite before review when the toolchain permits. For the
repository's x64 Debug workflow, see [Testing and performance](testing.md).

Ninja builds run the `lint` target as part of compilation. `clang-tidy` is
required by the preset, and configured first-party findings can fail the build.
Treat clang-tidy as a required build gate: fix applicable findings rather than
suppressing or bypassing checks. Report any unavailable toolchain component and
never claim unrun checks passed.
