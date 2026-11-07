# GraphicEngine Agent Contract

This repository is a Windows-first C++23 graphics engine. These rules apply to every agent working here. Repository procedures and specialist details live in the linked guides and skills; do not duplicate them here.

## Always-follow rules

- Preserve existing user changes. Keep edits scoped, inspect `git status` before staging, and never stage unrelated files.
- Treat `vendor/` as read-only: do not edit, format, or refactor third-party code.
- For implementation work, use a repository issue linked to the user-owned **Graphics Engine** project (#1), except for a narrowly scoped low-risk change that meets the lightweight path in [development workflow](docs/development-workflow.md). Keep project status current for tracked work. If project access is unavailable for work that does not qualify for the lightweight path, stop before implementation and report the blocker.
- Before implementation, identify the owning subsystem, affected consumers, and interface changes. Coordinate newly discovered cross-subsystem work through the issue before editing it.
- Preserve GPU resource ownership, lifetime, synchronization, and shutdown invariants. Follow existing backend boundaries and treat shader interfaces, serialization, and public headers as contracts.
- Never commit, push, or merge directly to `main`. Every change entering `main` must arrive through a PR. An AI agent must not approve or self-approve a PR targeting `main`; a human reviewer must approve it and required checks must pass. An agent may merge only through that approved PR after explicit user authorization.
- For parallel coding, give each agent an isolated worktree and non-overlapping task scope. Never have agents edit the same checkout concurrently.
- Do not access production systems, change credentials, or install SDKs, compilers, profilers, packages, or MCP servers without a task need and explicit authorization. Inspect setup scripts before running them.
- Do not claim checks, reviews, or merges passed unless the results were observed. Report unavailable tools and blocked checks accurately. A successful compile alone does not establish correct rendered output.
- Do not guess build, test, shader, or GPU-validation commands. Use the repository's documented commands and report hardware-specific checks that cannot run in the current environment.
- Measure a representative baseline before performance changes and keep workload, build, device/driver, resolution, and capture method with the result. Follow [testing and performance](docs/testing.md).
- When behavior, public APIs, architecture, or developer workflow changes, update the relevant docs and public-header comments. Add new documentation pages to `mkdocs.yml` navigation.

## Repository guidance

- [Development workflow](docs/development-workflow.md): issues, project status, branch strategy, agent isolation, commits, PRs, and handoffs.
- [Testing and performance](docs/testing.md): toolchain setup, builds, test discovery, CTest selection, and performance checks.
- [C++ guidelines](docs/cpp-guidelines.md): module layout, API design, ownership, formatting, and static analysis.
- Use a repository skill when its activation description matches the task. Skills add role-specific procedures and do not override this contract.

## Mem0

- Before planning, code generation, refactoring, debugging, tool setup, or architectural design, query Mem0 for project conventions, prior fixes, and system standards when its MCP is available for this project; verify results against repository evidence.
- After resolving complex bugs, establishing patterns, or changing build/system configuration, save verified, actionable facts tagged `[Category] [Domain]`. Never store secrets, source code, raw logs, or transient state.
