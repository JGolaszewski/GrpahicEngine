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

Before editing, record an impact map in the issue: the owning subsystem, affected
producers and consumers (including Core, D3D11, Editor, and Sandbox where relevant),
public or CPU/GPU contracts, documentation to update, and verification needed for
each affected area. Name the coding owner and acceptance criteria. If a dependency
or consumer is discovered later, update the issue and coordinate its owner before
editing that area. Keep a tightly coupled interface change in one issue and with
one coding owner. Split independently deliverable changes into child issues.

The coordinator checks project membership and Status before starting work and at
each PR transition. Use the live project's field and option IDs when updating
Status; do not copy IDs into scripts or instructions. Move a scoped issue to
`Ready`, to `In progress` when work starts, to `In review` when its PR opens, and
to `Done` only after its change merges to the intended target branch. If a PR is
closed without merging, return the issue to `Ready` or `In progress` according to
the remaining work. For an epic, a reviewed child PR merged to the epic can mark
the child `Done`; the parent remains active until the integration PR merges to
`main`. Verify issue state and board Status separately because they can differ.

## Branches, commits, and pull requests

For one issue, branch from `main` using `feat/<issue>-<slug>` or `fix/<issue>-<slug>`. Make at least one scoped Conventional Commit per task and include its issue reference, for example:

```text
feat(rendering): add shadow filtering (#123)
```

For at least two independently tracked child tasks, or work requiring parallel agents, create a parent issue and an `epic/<issue>-<slug>` integration branch from `main`. Give each child its own issue and branch from the epic branch; target child PRs to the epic. Integrate the child work there, preserve distinct feature commits, and open one integration PR to `main`.

Follow the PR, human-review, and merge boundary in `AGENTS.md`. Use the repository PR template and fill every section with observed evidence. The PR metadata check validates its required fields; it cannot verify the private project board, the truth of check results, or the quality of documentation. The coordinator and reviewer verify those claims.

## Parallel agents and handoffs

The coordinating agent owns issue breakdown, board updates, integration, and final verification. Choose one coding agent for tightly coupled cross-subsystem changes. Split work across specialist agents only when the interfaces and acceptance criteria permit independent changes; give each coding agent one issue, a narrow scope, and an isolated worktree/branch. If worktree isolation is unavailable, serialize coding and use other agents for read-only research or review. Never let two coding agents edit the same checkout.

Each handoff reports its issue and branch, commits, changed producers and consumers, interface impact, documentation changes, verification commands and observed results, and unresolved risks. The coordinator reviews the combined diff for integration gaps and verifies it before opening or updating the PR.

## Documentation and verification references

Update relevant docs and public-header API comments when behavior, public APIs, architecture, or developer workflow changes. Add new pages to `mkdocs.yml` navigation. If no docs are affected, state that in the PR summary instead of adding artificial documentation.

The coding owner updates documentation in the same change as the behavior or API.
The reviewer checks the affected guides and comments against the implementation.
Codex edit hooks are optional local feedback; only observed results belong in a
handoff. For C++ changes, build affected targets and run focused tests during
development, then run the full x64 Debug build, `lint`, and registered correctness
suite on the integrated branch when the toolchain is available. Compile shaders
and record a manual rendering check when GPU behavior changes. Record each
unavailable or unrun check with its reason; do not report it as passed.

The documentation workflow runs `scripts/build-docs.ps1` on relevant pull
requests. It lints source Markdown, checks the whole-engine API with warning-as-error
Doxygen settings, and builds the MkDocs Material site from Doxygen XML. A
successful push to `main` deploys the built site through the GitHub Pages
Actions artifact workflow; pull requests validate without deploying.

Use [Testing and performance](testing.md) for test discovery, focused and full CTest runs, build/lint requirements, and performance checks. Do not duplicate its test procedures here.
