# AGENTS.md — GraphicEngine C++23 Architecture and Workflow

## Repository facts

- This is a Windows-first C++23 graphics engine. Use Windows 11, Visual Studio 2022+, and a Visual Studio Developer PowerShell when building locally.
- Use CMake 3.28+ and the Ninja generator for the preset workflow. The root `CMakeLists.txt` currently declares a lower minimum; do not infer that the preset/toolchain workflow has been validated with that older version.
- Configure with the `x64-debug` preset. It expects `VCPKG_ROOT`, `clang-tidy`, and the Windows SDK `fxc` shader compiler to be available.
- Third-party source under `vendor/` is read-only. Never edit, format, or refactor it.

## Configure, build, test, and clean

Run these commands from the repository root in Developer PowerShell:

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug --parallel $env:NUMBER_OF_PROCESSORS
ctest --test-dir out/build/x64-debug --output-on-failure -C Debug
cmake --build out/build/x64-debug --target clean
```

The root `CMakeLists.txt` sets `CMAKE_EXPORT_COMPILE_COMMANDS=ON`. The compilation database is at `out/build/x64-debug/compile_commands.json`. If a tool needs it at the repository root, copy it with:

```powershell
Copy-Item out/build/x64-debug/compile_commands.json compile_commands.json -Force
```

The test project builds `GraphicEngineUnitTests` from `GraphicEngineCore/tests/unit/` with GoogleTest, plus `AssetManagerTests` (CTest label `integration`) and `BrdfReferenceTests` (CTest label `unit`). Use CTest labels or `ctest -N` to select and inspect tests; the `engine-unit-tests`, `engine-integration-tests`, and `engine-tests` targets are stable build-and-run entry points. D3D11 shader sources are compiled by the `CompileShaders` target using `fxc` and shader-model 5.0 profiles. Build that target when changing shaders:

```powershell
cmake --build out/build/x64-debug --target CompileShaders
```

## Module layout and dependencies

- The engine subprojects are `GraphicEngineCore/` and `GraphicEngineEditor/`; do not create monolithic `include/` or `src/` directories at either subproject root.
- Core modules are `GraphicEngineCore/modules/{assets,rendering,runtime,scene}/`. Editor modules are `GraphicEngineEditor/modules/{application,commands,events,panels,platform}/`.
- Each module owns a `CMakeLists.txt`, public headers in `include/`, and implementation files plus private headers in `src/`. Keep module-specific headers directly in `include/` or in meaningful domain folders. Do not add a redundant `include/<same-module-name>/` or `src/<same-module-name>/` layer.
- Follow the existing include conventions and the include directories exported by module targets. In particular, do not reintroduce paths that rely on the removed subproject-level include directories.
- Keep public headers limited to API needed by consumers. Prefer forward declarations, move implementation-only headers into `src/`, and use PIMPL where it materially reduces graphics API or third-party header exposure.
- Keep module dependencies explicit in each module's CMake target. Prefer dependencies from lower-level facilities toward higher-level features and avoid adding cycles. The Core modules still have some legacy cross-module references and are bundled into `GraphicEngineCore`; treat a fully acyclic graph as an ongoing migration goal, not an already-enforced property. Extract shared types or interfaces before introducing a new cycle.
- Put shader assets with the backend or module that owns them. Current D3D11 shaders live in `GraphicEngineBackends/D3D11/shaders/`; do not create empty shader directories.

## C++23 practices

- Use the existing `Engine::Result<T>` (`std::expected`) for operations that can fail and `std::optional` for optional values. Do not throw from per-frame rendering paths.
- Use RAII for ownership. Prefer `std::unique_ptr` for exclusive heap ownership, `std::span` and `std::string_view` for non-owning views, and `Microsoft::WRL::ComPtr` for D3D COM interfaces. Do not manually release owned graphics resources outside their RAII owner.
- Avoid raw owning pointers, needless copies of large geometry data, and pointer-heavy structures in hot render paths. Prefer contiguous storage and measure before changing performance-sensitive code.
- Do not use `using namespace std;` or namespace imports in headers. Keep public symbols inside the module's established `Engine` namespace.
- `ENGINE_ASSERT` is not currently defined in this repository. Do not use it unless a shared assertion facility is added; use `assert` for appropriate debug invariants and `Engine::Result` for recoverable failures.
- Preserve GPU data layout explicitly. Keep constant-buffer structures correctly aligned and validate CPU/GPU layout assumptions when changing them.

## Shaders and static analysis

- Keep first-party shader sources out of `vendor/`. For current D3D11 shaders, use the CMake `CompileShaders` target and `fxc`; do not assume DXC or Vulkan shader validators are configured in this repository.
- `.clang-tidy` enables `bugprone-*`, `modernize-*`, `performance-*`, `cppcoreguidelines-*`, and `readability-*`, with the documented graphics exceptions. `bugprone-*` and `performance-*` warnings are errors.
- First-party targets built with MSVC use `/W4 /WX /permissive- /analyze /w14242 /w14265`; these options are target-scoped and are not applied to vendored targets.
- The header filter is adapted to this repository's actual paths (`GraphicEngineCore`, `GraphicEngineEditor`, `GraphicEngineBackends`, and `SandboxApp`); `vendor/`, `out/`, and `build/` are excluded. CMake requires clang-tidy. Ninja/Makefile builds run the `lint` target as part of the build; other generators attach clang-tidy to first-party C++ compilation.
- Third-party include directories must be marked `SYSTEM` in CMake. Preserve the existing SYSTEM treatment for ImGui, NVRHI, tinygltf, and stb; do not add vendor include paths as ordinary project includes.
- Use `.clang-format` for formatting. Format only touched first-party C++ files; never format vendor code. The CMake `format` target checks formatting without modifying files.

## Change and verification discipline

- For directory moves, CMake changes, and shader changes, configure and build the affected targets before committing. Run the relevant CTest cases for behavior changes. If a required toolchain component is unavailable, report that clearly and do not claim verification passed.
- Keep changes scoped and use descriptive conventional commit messages, for example `refactor(rendering): isolate render graph module` or `build: enforce clang-tidy checks`.
- Do not stage unrelated working-tree changes. Review `git status` and the staged diff before committing.

## GitHub Project and multi-agent workflow

- Use the repository's user-owned **Graphics Engine** GitHub Project (#1) for implementation work. Each task must have a repository issue linked to this project; check for an existing issue before creating one. Do not use a project-only draft item as the task record.
- Keep the project Status current: `Backlog` while unstarted, `Ready` when scope and acceptance criteria are clear, `In progress` while implementation is active, `In review` when a PR is open, and `Done` after the change is merged. If project access is unavailable, stop before implementation and report the blocker.
- For one task, branch from `main` using `feat/<issue>-<slug>` or `fix/<issue>-<slug>`. Do not commit directly to `main`.
- For work with at least two independently tracked child tasks, or work that needs parallel coding agents, create a parent issue and an `epic/<issue>-<slug>` integration branch from `main`. Each child issue gets a task branch based on the epic branch and a PR targeting that branch. Open one integration PR from the epic branch to `main` after the child work is integrated.
- Make at least one scoped Conventional Commit per feature/task and include its issue reference, for example `feat(rendering): add shadow filtering (#123)`. Do not mix unrelated tasks in a commit or squash distinct feature commits together during epic integration.
- The root/coordinating agent owns task breakdown, board transitions, integration, and final verification. Give each coding agent one issue and an isolated worktree/branch. If isolated worktrees are unavailable, parallelize read-only investigation/review or work on non-overlapping files; do not have multiple agents edit the same checkout concurrently.
- Require each agent handoff to report its issue, branch, commits, changed areas, build/lint/test commands and results, and remaining risks. The coordinator verifies the integrated result before review.

## Documentation and verification workflow

- Update the relevant `docs/` guide and public-header API comments when a change affects documented behavior, public APIs, architecture, or developer workflow. Add new pages to `mkdocs.yml` navigation. If documentation is unaffected, state that in the PR summary instead of making an artificial docs edit.
- Before adding or changing tests, inspect the current checkout's CMake test registration and CTest inventory. They are the source of truth for available targets, labels, and frameworks; do not assume that GitHub `main` or older documentation matches the checkout.
- Add a focused regression test for each bug fix and tests for new deterministic behavior, important edge cases, error results, and boundary conditions. Keep unit tests fast and independent of windows, GPU devices, services, and unnecessary filesystem state. Use integration tests when real subsystem/platform interaction is necessary. Avoid coupling tests to private implementation details.
- For a test change, run the focused CTest selection first, then the full registered CTest suite before review when the toolchain permits. Use the test framework and target layout already present in the checkout; do not introduce another framework without a concrete need.
- **Ninja builds run the `lint` target as part of the build.** `clang-tidy` is required by the preset, and configured first-party findings can fail the build. Treat clang-tidy failures as build failures: fix applicable first-party findings, do not suppress or bypass checks to make a build pass, and report missing tooling or blocked checks explicitly.
- Follow the existing x64 Debug configure/build and CTest commands in this file. Include build, clang-tidy, and relevant CTest results in the final handoff; never claim a check passed unless it completed successfully.
