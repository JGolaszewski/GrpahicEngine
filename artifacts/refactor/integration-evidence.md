# Refactor integration evidence (pre-main)

Date: 2026-10-15. Validation checkout: `codex/refactor-integration`. Integration base: local `main` at `672764d08c58330db988f39069c3a183035c277d`.

## Ordered commits and branch refs

Each `codex/` branch below will point at exactly one commit ahead of its prerequisite. The original feature worktrees remain preserved because one contains uncommitted user changes.

| Order | Branch ref | Commit | Change | Validation evidence |
|---|---|---|---|---|
| 1 | `codex/scene-hierarchy` | `f4b7f8c592adf3198a18fb7ee0f3960e925d7bdc` | Scene hierarchy and outliner | Included in full Debug build and correctness suite. |
| 2 | `codex/transactional-scene-creation` | `4df1b3e21e150d0e22a89d684f7d7763ee5032fb` | Transactional scene creation | Feature configure/build/test logs are saved under `feature-scene-creation-*`. |
| 3 | `codex/clang-tidy-target` | `a6a7e542ab7871b1adb728e34e61c70e0c1947fd` | Separate clang-tidy from compile targets | Final lint run processes 77 translation units with zero actionable diagnostics. |
| 4 | `codex/engine-suite-benchmarks` | `a5ef8c2bf7f75d2381303044ca5383c3c9f762ac` | Comprehensive engine suite and benchmark harness | Feature build/test logs saved under `feature-tests-*`. |
| 5 | `codex/deferred-pbr` | `4c9974d314e4b4719c4034f47759a1d5223a8b28` | Deferred PBR lighting | Feature build/test logs saved under `feature-lighting-*`. |
| 6 | `codex/core-boundaries` | Pending amended refactor commit | Core backend boundaries, coverage, docs, and verification | Full Debug build and 82/82 tests pass; clean-directory workflow also passes. |

The aliases are cumulative by dependency: each branch adds exactly one commit after the previous row. Merge them to `main` with `--ff-only` in this order, then record the actual command results and test result for every branch.

## Current gates

| Metric | Result | Evidence |
|---|---|---|
| Full x64 Debug build | Pass, 0 compiler warnings/errors in clean build | `integration-clean-build.log` |
| Correctness suite | Pass, 82/82 | `integration-clean-ctest.log` |
| Core production coverage | Pass: 85.27% lines, 70.06% branches | `integration-core-coverage-verify.txt`, `.json`, and `.profdata` |
| Critical scenarios | Pass: hierarchy/transforms, persistence/migrations, asset imports, events, renderer lifecycle, command history | `integration-clean-ctest.log` |
| Benchmarks | Release x64 benchmark report covers transforms, traversal, scene save, and render graph | `integration-benchmarks-release.json`, `integration-benchmarks-metadata.json` |
| Backend boundary | Pass: 0 prohibited public-header matches and 0 editor native/backend matches | `architecture-scan-final.txt` |
| Dead-code review | Reviewed candidates have documented dispositions; no confirmed unused project-owned dependency/symbol remains in the reviewed set | `dead-code-candidate-review-final.md` |
| Core Doxygen | Pass; 0 warnings/errors; warnings-as-errors enabled | `integration-doxygen-final.log`, `docs/Doxyfile` |
| Formatting | Pass | `integration-format-final.log` |
| Clang-tidy | Pass; 77 translation units, 0 actionable diagnostics | `integration-clang-tidy-final.log` |
| Reproducible workflow | Pass from fresh `out/build/x64-debug-clean`: preset configure, full build, and 82/82 CTest | `integration-clean-configure.log`, `integration-clean-build.log`, `integration-clean-ctest.log` |
| Git history / final main | Pending ordered one-commit branch audit, fast-forward merges, final main gates, and clean-tree check. |

This is an integration-branch checkpoint, not the final completion record. Update it with the final main hash, per-branch build/test results, final gate results, and clean-tree check before marking the goal complete.
