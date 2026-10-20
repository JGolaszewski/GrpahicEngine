# Agentic development

Use this guide when an AI agent helps develop GraphicEngine. The root
`AGENTS.md` is the contract for every agent. [Development workflow](development-workflow.md)
defines issue, branch, project, and PR rules; [Testing and performance](testing.md)
defines the verification commands. Start with [Getting started](getting-started.md)
if the Windows toolchain or build tree is not ready.

## Start a task

1. Find or create a repository issue linked to the user-owned
   [Graphics Engine project](https://github.com/users/JGolaszewski/projects/1).
   Record the coding owner, affected producers and consumers, interface contracts,
   docs, verification, and acceptance criteria. Check issue state and project
   Status separately. Stop before implementation if project access is unavailable.
2. Choose one task branch for a coupled change. For independent child issues,
   create an epic integration branch and a separate branch and worktree for each
   coding agent. Never let coding agents edit the same checkout concurrently.
3. Apply the relevant skills below. Inspect the affected subsystem and its
   consumers before editing. If another dependency appears, update the issue and
   ownership assignment before changing that area. Keep `vendor/` read-only.
4. Change code, focused tests, relevant docs, and public-header comments together.
   Run focused checks while developing, then verify the integrated diff according
   to [Testing and performance](testing.md). Record exact results and blockers.
5. Have a separate reviewer inspect the diff and evidence. When a PR is requested,
   fill the repository PR template, move the issue to **In review**, and preserve
   the human approval and merge boundary in `AGENTS.md`. Move it to **Done** only
   after the change merges to its intended branch.

## Skills and subagents

A **skill** is a set of role instructions in `.codex/skills/`; it does not start a
second agent. A **subagent** is a separate worker that can use a relevant skill.
Name a skill in a task when a particular role is needed, or let its activation
description select it. The root contract still applies to every role.

| Skill | Use it for | Expected handoff |
| --- | --- | --- |
| `ge-coordinator` | Issue and board tracking, impact maps, task division, and integrated delivery | Issue and branch state, combined impact, checks, risks |
| `ge-cpp-coder` | First-party Core, D3D11 backend C++, Editor, and their CMake wiring | Producer/consumer and API changes, tests, docs |
| `ge-shader-engineer` | D3D11 HLSL, shader compilation, and CPU/GPU layouts | Layout agreement, shader compile, rendering evidence |
| `ge-qa-reviewer` | Independent, read-only review of the combined diff | Findings with locations, observed checks, gaps |
| `ge-devops` | Worktrees, Git/GitHub coordination, CI investigation, and PR preparation | Exact branch, worktree, PR, and check state |

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

For the focused fix below, an assignment could look like this:

```text
Issue: #<number>, In progress on the Graphics Engine project
Owner: ge-cpp-coder; Core scene loading
Branch/worktree: fix/<number>-scene-load; out/worktrees/<number>-scene-load
Consumers: inspect Editor and Sandbox scene-open paths
Contract: valid scenes still load; the reported failure gets a regression test
Docs: update the scene guide and public API comments if their contract changes
Checks: focused regression, affected build, integrated Debug build/lint/CTest
Handoff: commits, changed consumers, exact results, docs, and remaining risks
```

## Optional Codex hooks

[Codex hooks](codex-hooks.md) has the registration TOML and setup instructions.
They run only when enabled and registered in the user's Codex configuration.
Run Codex from the repository root so the relative script paths resolve.

| Hook | Trigger and prerequisites | Feedback |
| --- | --- | --- |
| C++ edit loop | After `apply_patch`, `Edit`, or `Write` touches an eligible first-party C/C++ file and `out/build/x64-debug/CMakeCache.txt` exists | Best-effort `clang-format`, existing Debug build, `lint`, and `unit` or `integration` CTest selection; 300-second overall deadline |
| AddressSanitizer | After a `Bash` command containing `ctest`, except one targeting `out/build/x64-asan`; needs cached Ninja and installed vcpkg paths | Configure and build `x64-asan`, then run its full CTest suite |

The edit hook skips documentation-only changes, excluded directories, and a
checkout without the Debug build cache. The ASan hook can take longer because
it builds another tree. A hook reports failures **after** the triggering tool
operation; its `block` result gives the agent feedback but cannot undo an edit
or a command's side effects. Tool operations outside the registered matchers do
not trigger these hooks. Treat a skipped, unavailable, or failed hook as such in
the handoff. Run and report the explicit checks required for the task; hooks do
not replace [testing](testing.md) or PR checks.

## Worked workflows

### Focused Core fix

Example request: "Fix a scene loading failure, keep existing behavior for valid
scenes, and show the regression test."

1. The coordinator finds the issue, records Core as owner and checks whether the
   Editor or Sandbox consumes the affected scene API. The issue is **In progress**
   while the `fix/<issue>-<slug>` branch is being changed.
2. One `ge-cpp-coder` owner reproduces the failure, adds a focused regression
   test, fixes the responsible layer, and updates the scene guide or API comments
   if the observable contract changes. If the fix reaches Editor or Sandbox,
   the owner updates the impact map before editing those consumers.
3. The owner runs the focused CTest selection, affected build, then the
   integrated Debug build, `lint`, and correctness suite when available. A
   `ge-qa-reviewer` checks the regression, consuming paths, docs, and exact
   results. The coordinator records blockers and prepares the PR when requested.

### Coupled rendering change

Example request: "Add a rendering option exposed in the Editor that changes a
D3D11 shader input."

1. The coordinator maps the producer and consumer contract from Core through
   D3D11 to the Editor, including CPU/GPU layout, shader bindings, and Sandbox
   behavior if it consumes the option. One coding owner keeps the coupled change
   on one issue and branch.
2. The owner applies `ge-cpp-coder` for C++ and `ge-shader-engineer` for HLSL or
   layout work. The same change updates the relevant rendering and Editor guides
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
