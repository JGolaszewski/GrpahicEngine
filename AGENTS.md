# GraphicEngine Agent Contract

This repository is a Windows-first C++23 graphics engine. These rules apply to every agent working here. Repository procedures and specialist details live in the linked guides and skills; do not copy them into this file.

## Always-follow rules

- Preserve existing user changes. Keep edits scoped, inspect `git status` before staging, and never stage unrelated files.
- Treat `vendor/` as read-only: do not edit, format, or refactor third-party code.
- For implementation work, use a repository issue linked to the user-owned **Graphics Engine** project (#1). Keep project status current as described in [development workflow](docs/development-workflow.md). If project access is unavailable, stop before implementation and report the blocker.
- Never commit, push, or merge directly to `main`. Every change entering `main` must arrive through a PR. An AI agent must not approve or self-approve a PR targeting `main`; a human reviewer must approve it and required checks must pass. An agent may merge only through that approved PR after explicit user authorization.
- For parallel coding, give each agent an isolated worktree and non-overlapping task scope. Never have agents edit the same checkout concurrently.
- Do not claim checks, reviews, or merges passed unless the results were observed. Report unavailable tools and blocked checks accurately.
- When behavior, public APIs, architecture, or developer workflow changes, update the relevant docs and public-header comments. Add new documentation pages to `mkdocs.yml` navigation.

## Repository guidance

- [Development workflow](docs/development-workflow.md): issues, project status, branch strategy, agent isolation, commits, PRs, and handoffs.
- [Testing and performance](docs/testing.md): toolchain setup, builds, test discovery, CTest selection, and performance checks.
- [C++ guidelines](docs/cpp-guidelines.md): module layout, API design, ownership, formatting, and static analysis.
- Use a repository skill when its activation description matches the task. Skills add role-specific procedures and do not override this contract.
