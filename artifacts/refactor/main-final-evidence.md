# Refactor completion evidence — local main

Validation commit: `616a68c948d980399c59edf6f91a844a4d956a1a` (`main`, 2026-10-15). The subsequent evidence commit changes only reports.

## Final metric table

| Area | Result | Evidence |
|---|---|---|
| Full x64 Debug build | Pass; 0 compiler errors and 0 warnings in fresh build output | `integration-clean-build.log`, `main-final-build.log` |
| Correctness suite | Pass; 82/82 tests on final main | `main-final-ctest.log` |
| Core production coverage | Pass; 85.27% lines, 70.06% branches; 34 production source files | `main-final-coverage.txt`, `.json`, `.profdata`, `main-final-coverage-ctest.log` |
| Critical scenarios | Tests cover transforms/hierarchy, persistence and migration, asset loading/import failures, events, renderer lifecycle, and editor command history | `main-final-ctest.log`; feature-stage logs under `feature-*-ctest.log` |
| Benchmarks | Repeatable Release x64 results for transforms, scene traversal, scene-document saving, and render-graph build/compile | `main-final-benchmarks.json`, `main-final-benchmarks-metadata.json` |
| Backend boundary | Pass; 0 prohibited backend includes/types in 38 public core headers and 0 editor backend/native graphics matches | `main-final-architecture-scan.txt` |
| Dead-code candidate review | All recorded candidates have dispositions; no confirmed unused project-owned dependency or symbol remains in the reviewed set | `main-final-dead-code-review.md` |
| Core Doxygen | Pass; zero warnings/errors with warnings-as-errors enabled | `main-final-doxygen.log`, `docs/Doxyfile` |
| Formatting | Pass | `main-final-format.log` |
| Clang-tidy | Pass; 77 translation units, zero actionable diagnostics | `main-final-clang-tidy.log` |
| Reproducibility | Pass; preset configured a fresh directory, full x64 Debug build and 82/82 tests passed | `integration-clean-configure.log`, `integration-clean-build.log`, `integration-clean-ctest.log` |
| History and working tree | Six ordered integration commits fast-forwarded into local main. Each `codex/` branch pointer is one commit ahead of its prerequisite. Evidence-only commit follows. Existing feature worktrees with user changes were preserved. | `main-final-merge-log.txt`; final `git status --short` recorded after evidence commit |

## Ordered fast-forward record

| Branch | Commit | Change | Build/test evidence |
|---|---|---|---|
| `codex/scene-hierarchy` | `f4b7f8c592adf3198a18fb7ee0f3960e925d7bdc` | Scene hierarchy and outliner | Cumulative final build and 82/82 main suite pass. |
| `codex/transactional-scene-creation` | `4df1b3e21e150d0e22a89d684f7d7763ee5032fb` | Transactional scene creation | `feature-scene-creation-ctest.log`: 3/3 pass. |
| `codex/clang-tidy-target` | `a6a7e542ab7871b1adb728e34e61c70e0c1947fd` | Separate lint from compile | Final Debug build and 82/82 main suite pass; final lint report is clean. |
| `codex/engine-suite-benchmarks` | `a5ef8c2bf7f75d2381303044ca5383c3c9f762ac` | Correctness suite and benchmark harness | `feature-tests-ctest.log`: 20/20 pass. |
| `codex/deferred-pbr` | `4c9974d314e4b4719c4034f47759a1d5223a8b28` | Deferred PBR lighting | `feature-lighting-ctest.log`: 20/20 pass. |
| `codex/core-boundaries` | `616a68c948d980399c59edf6f91a844a4d956a1a` | Core backend boundaries and verification | Full clean build, 82/82 tests, coverage, formatting, lint, and Doxygen pass. |

The branch refs were merged in the listed order using `git merge --ff-only`. The test suite expanded as the feature branches added cases; final main validates all 82 registered cases. Original topic worktrees were left intact.
