# Refactor completion evidence — local main

Code merge tip: `616a68c948d980399c59edf6f91a844a4d956a1a`. Subsequent commits contain verification reports only. Final branch is local `main`.

## Final metric table

| Area | Result | Evidence |
|---|---|---|
| Full x64 Debug build | Pass; 0 compiler errors and 0 project compiler warnings | `main-final-build.log` |
| Correctness suite | Pass; 82/82 tests on final main | `main-final-ctest.log` |
| Branch correctness suites | Pass before merge: 3/3, 3/3, 3/3, 20/20, 20/20, 82/82 | `branch-audit/branch-audit-results.json` and per-branch logs |
| Core production coverage | Pass; 85.27% lines, 70.06% branches across 34 production source files | `main-final-coverage.txt`, `.json`, `.profdata`, `main-final-coverage-ctest.log` |
| Critical scenarios | Transforms/hierarchy, persistence/migration, asset loading/import errors, events, renderer lifecycle, and command history covered | `main-final-ctest.log` and branch-stage CTest logs |
| Benchmarks | Release x64 results for transforms, scene traversal, scene-document saving, and render-graph build/compile | `main-final-benchmarks.json`, `main-final-benchmarks-metadata.json` |
| Backend boundary | Pass; 0 prohibited public-header matches in 38 core headers and 0 editor backend/native graphics matches | `main-final-architecture-scan.txt` |
| Dead-code candidate review | Every recorded candidate has a disposition; no confirmed unused project-owned symbol/dependency remains in the reviewed set | `main-final-dead-code-review.md` |
| Core Doxygen | Pass; zero warnings/errors with warnings-as-errors enabled | `main-final-doxygen.log`, `docs/Doxyfile` |
| Formatting | Pass | `main-final-format.log` |
| Clang-tidy | Pass; 77 translation units, zero actionable diagnostics, real tool | `main-final-clang-tidy.log` |
| Reproducibility | Pass from fresh x64 Debug build directory; configure, full build, and 82/82 tests | `integration-clean-configure.log`, `integration-clean-build.log`, `integration-clean-ctest.log` |
| Git history / final tree | Six one-commit branches fast-forwarded in dependency order; final working tree clean after evidence commit | `main-final-merge-log.txt`; current `git status --short` |

## Branch-stage test records

Each branch was checked out at its exact commit in an isolated validation worktree, built, and tested before replaying its fast-forward into `main`. Historical branch states that attached clang-tidy to the default build used a no-op clang-tidy shim only for those intermediate correctness builds. The final main lint gate ran separately with the real clang-tidy tool and passed all 77 translation units.

See `branch-audit/branch-audit-results.json` for branch and prerequisite hashes, build status, and test counts. Each build and CTest log is stored alongside it.
