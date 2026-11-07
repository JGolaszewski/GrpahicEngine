# Agentic development

Use this guide when an AI agent helps develop GraphicEngine. The root
`AGENTS.md` is the contract for every agent. [Development workflow](development-workflow.md)
defines issue, branch, project, and PR rules; [Testing and performance](testing.md)
defines the verification commands. Start with [Getting started](getting-started.md)
if the Windows toolchain or build tree is not ready.

For small, reversible, single-subsystem fixes, first check whether the change
qualifies for the [lightweight workflow](development-workflow.md#lightweight-path-for-low-risk-changes).
Use the full issue and project workflow for broader or contract-changing work.

## Start a task

1. For tracked work, find or create a repository issue linked to the user-owned
   [Graphics Engine project](https://github.com/users/JGolaszewski/projects/1).
   Record the coding owner, affected producers and consumers, interface contracts,
   docs, verification, and acceptance criteria. Check issue state and project
   Status separately. Stop before implementation if project access is unavailable.
   For lightweight work, record the reason, owner, acceptance criterion, and
   focused check in the handoff instead.
2. Choose one task branch for a coupled change. For independent child issues,
   create an epic integration branch and a separate branch and worktree for each
   coding agent. Never let coding agents edit the same checkout concurrently.
3. Apply the relevant skills below. Use focused Graphify queries on the changed
   symbols or files to find connected systems and consumers that may need
   accommodation. Verify graph leads against source, build metadata, and tests;
   if the graph is missing or stale, record that and trace callers directly.
   Update the impact map and ownership before editing newly discovered areas.
   Keep `vendor/` read-only.
4. Change code, focused tests, relevant docs, and public-header comments together.
   Run focused checks while developing, then verify the integrated diff according
   to [Testing and performance](testing.md). Record exact results and blockers.
5. Have a separate reviewer inspect the diff and evidence. When a PR is requested,
   fill the repository PR template, move the issue to **In review**, and preserve
   the human approval and merge boundary in `AGENTS.md`. Move it to **Done** only
   after the change merges to its intended branch.

## Skills and subagents

A **skill** is a set of role instructions in `.agents/skills/`; it does not start a
second agent. A **subagent** is a separate worker that can use a relevant skill.
Name a skill in a task when a particular role is needed, or let its activation
description select it. The root contract still applies to every role.

| Skill | Use it for | Expected handoff |
| --- | --- | --- |
| `cpp-engineering` | First-party C++ implementation, refactoring, ownership, and interface work | Affected consumers, API/lifetime impact, focused checks |
| `engine-change-impact` | Cross-subsystem, renderer, backend, asset-format, or public-contract changes | Evidence-backed impact map and regression surface |
| `engine-environment-audit` | Toolchain setup, onboarding, or environment-gap diagnosis | Observed tool/version matrix and explicit unknowns |
| `graphics-api-research` | Version-sensitive graphics API, shader language, compiler, driver, or library questions | Primary-source citations with version and platform scope |
| `native-dependency-review` | Native dependencies, build inputs, downloads, parsers, or supply-chain review | Findings tied to manifests/scripts and evidence limits |
| `renderer-performance` | Measured CPU/GPU performance or frame-pacing work | Comparable baseline, controlled result, correctness evidence |
| `renderer-validation` | Rendering behavior, GPU lifetime, synchronization, or visual output changes | Exact checks, device/backend context, unverified coverage |
| `shader-pipeline-validation` | Shader source, variants, reflection, generated bindings, or CPU/GPU layouts | Checked variants, interface evidence, and runtime limits |
| `editor-ui-development` | Editor panel, menu, widget, input, docking, or local preference changes | Event path, persistence owner, focused checks, and observed UI behavior |
| `editor-feature-integration` | Exposing a Core, scene, asset, or renderer feature through a complete Editor workflow | Engine-to-Editor impact map, request/state flow, persistence, docs, and validation |

Specialist agents in `.codex/agents/` provide focused exploration, impact
analysis, build triage, graphics documentation research, environment inventory,
capture analysis, review, Editor UI implementation, and engine-to-Editor
integration. Their `developer_instructions` define the role boundary; a
specialist agent does not replace the coding owner's integrated review.

## Research and context tools

Use configured tools when available. Graphify helps trace connected code systems;
for specific library/API documentation in the local docs RAG corpus, query RAG
first and check its version context; use Exa or another approved web tool for
corpus gaps or current/version-sensitive confirmation. The corpus and its limits
are listed in the [tooling guide](../.codex/agent-tooling.md). Sequential
thinking can structure complex analysis; Mem0 provides durable project context.
These tools are declared by the user's Codex environment and may be temporarily
unavailable during workflow upgrades. If unavailable, use the documented source
or repository-search fallback. Verify retrieval against current source before
acting; graph, memory, and search results are leads, not authority. See the shared
[agent tooling guide](../.codex/agent-tooling.md) for exact use and limits.

Use one coding owner when Core, D3D11, Editor, or Sandbox changes must agree on
one evolving interface. The owner can apply both C++ and shader skills. Delegate
bounded read-only investigation or review without splitting ownership. Parallel
coding fits independent deliverables with separate issues, branches, worktrees,
and non-overlapping edit scopes. The coordinator integrates and verifies their
combined result; a specialist handoff is not an integration check.

Give each coding agent a task packet containing the issue and target branch,
worktree path, owned files or subsystem, affected consumers, interface contract,
acceptance criteria, required docs and checks, and handoff format. Use the
[architecture guide](architecture.md) to trace dependencies before assigning
work. If isolation is unavailable, serialize coding and use additional agents
only for read-only work.

Use `git-wt` (Worktrunk) to create and verify each isolated coding worktree. For
the focused fix below, an assignment could look like this:

```text
Issue: #<number>, In progress on the Graphics Engine project
Owner: C++ implementation skill; Core scene loading
Branch/worktree: fix/<number>-scene-load; created and verified with `git-wt`
Consumers: inspect Editor and Sandbox scene-open paths
Contract: valid scenes still load; the reported failure gets a regression test
Docs: update the scene guide and public API comments if their contract changes
Checks: focused regression, affected build, integrated Debug build/lint/CTest
Handoff: commits, changed consumers, exact results, docs, and remaining risks
```

## Codex hooks

[Codex hooks](codex-hooks.md) documents project-local hooks for protected paths,
agentic setup, changed-file formatting and input checks, validation reminders,
and explicit GPU diagnostic sessions. The setup checker and protected-path rule
also run in CI. Broad builds, lint, CTest, ASan, visual scenarios, and GPU
captures remain task-scoped verification. A `PostToolUse` hook reports feedback
after its triggering operation and cannot undo that operation.

## Worked workflows

### Focused Core fix

Example request: "Fix a scene loading failure, keep existing behavior for valid
scenes, and show the regression test."

1. The coordinator finds the issue, records Core as owner and checks whether the
   Editor or Sandbox consumes the affected scene API. The issue is **In progress**
   while the `fix/<issue>-<slug>` branch is being changed.
2. One coding owner using `cpp-engineering` reproduces the failure, adds a
   focused regression test, fixes the responsible layer, and updates the scene
   guide or API comments if the observable contract changes. If the fix reaches Editor or Sandbox,
   the owner updates the impact map before editing those consumers.
3. The owner runs the focused CTest selection, affected build, then the
   integrated Debug build, `lint`, and correctness suite when available. A
   `test-reviewer` checks the regression, consuming paths, docs, and exact
   results. The coordinator records blockers and prepares the PR when requested.

### Coupled rendering change

Example request: "Add a rendering option exposed in the Editor that changes a
D3D11 shader input."

1. The coordinator maps the producer and consumer contract from Core through
   D3D11 to the Editor, including CPU/GPU layout, shader bindings, and Sandbox
   behavior if it consumes the option. One coding owner keeps the coupled change
   on one issue and branch.
2. The owner applies `cpp-engineering` for C++ and
   `shader-pipeline-validation` for shader or layout work. The same change updates
   the relevant rendering and Editor guides
   and public-header comments. A new consumer is added to the issue before its
   code is edited.
3. The owner checks CPU/GPU layout agreement, builds `CompileShaders`, runs
   affected C++ tests and integrated build/lint/CTest checks, and records a manual
   rendering check for changed GPU output. The reviewer traces the option through
   its consumers and reports any unverified GPU behavior explicitly.

### Parallel epic

Example request: "Deliver an independent Core asset-validation improvement and
an Editor shortcut improvement as one reviewed milestone."

1. The coordinator creates a parent issue and `epic/<issue>-<slug>` branch from
   `main`. Each independently deliverable change gets a child issue, an assigned
   owner, a branch from the epic, and an isolated worktree. Child scopes name
   their separate files and acceptance criteria.
2. Each owner updates its own tests and docs, runs focused checks, and hands off
   commits, interface impact, and observed results. Child PRs target the epic.
   Read-only reviewers inspect them; reviewed child changes merged into the epic
   can move their child issues to **Done**.
3. The coordinator checks the combined diff and runs integrated verification on
   the epic. Its parent issue stays active until the integration PR to `main`
   passes required checks, receives human approval, and is merged with explicit
   user authorization. The parent then moves to **Done**.

For every workflow, the final handoff states the issue and board Status, branch
and worktree, changed producers and consumers, interface and documentation
impact, exact verification commands and results, PR state, and remaining risks.
The [development workflow](development-workflow.md) remains the source for status
transitions and review rules.
