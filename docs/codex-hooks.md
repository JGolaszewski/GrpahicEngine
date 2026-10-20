# Codex hooks

The repository provides Python hooks for a fast C++ edit loop and explicit CTest runs. They are optional local automation; build, lint, and test requirements remain documented in [Testing and performance](testing.md). See [Agentic development](agentic-development.md) for how hooks fit into task handoffs.

## Register the hooks

Enable the `hooks` feature in your Codex configuration (`hooks = true` in the `[features]` table), then add the following entries. Keep personal approval, sandbox, and connector settings in your own configuration rather than copying them into the repository.

```toml
[[hooks.PostToolUse]]
matcher = "^(apply_patch|Edit|Write)$"

[[hooks.PostToolUse.hooks]]
type = "command"
command = "python .codex/hooks/build_after_edit.py"
command_windows = "python .codex/hooks/build_after_edit.py"

[[hooks.PostToolUse]]
matcher = "^Bash$"

[[hooks.PostToolUse.hooks]]
type = "command"
command = "python .codex/hooks/sanitizer_check.py"
command_windows = "python .codex/hooks/sanitizer_check.py"
```

Run Codex from the repository root so the relative script paths resolve.

## C++ edit hook

After an `apply_patch`, `Edit`, or `Write` operation, the hook normalizes Windows and POSIX paths and selects changed C/C++ files under the repository. It ignores vendor, third-party, and build output directories. If no eligible files changed or `out/build/x64-debug/CMakeCache.txt` is missing, it exits without running commands.

When `clang-format` is available, it formats only the selected files. Formatting is best-effort. It then builds the existing x64 Debug tree, runs the `lint` target, and executes CTest tests with the exact `unit` or `integration` labels, excluding `benchmark`. It does not configure the build. Build, lint, and test failures report a blocking result to the agent after the edit, with a bounded output tail. The result does not undo the edit. The hook has a 300-second timeout.

## Explicit CTest hook

After a `Bash` tool command that invokes `ctest` outside `out/build/x64-asan`, the hook reuses the debug tree's cached Ninja and vcpkg paths to configure and build the x64 ASan preset, then runs its full CTest suite. Configuration, build, and test failures report a blocking result with a bounded diagnostic output after the command. Commands that already target the ASan tree are skipped to avoid recursion.

The ASan hook can take longer than the edit hook because it configures and builds a separate tree. Use it for explicit CTest commands, and report a missing CMake executable, cached Ninja, or installed vcpkg tree as a local setup blocker.
