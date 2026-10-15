# Graphics engine refactor completion plan

The refactor is complete only when every gate below passes on the final local
`main` branch. Each build, test, and analysis result must be saved under
`artifacts/refactor/` and named with the branch or final audit stage.

## 1. Stabilize the working branch

- Identify the dependency order of the existing renderer, object model, editor,
  and test branches from their actual commit graph.
- Preserve the current working tree while collecting the refactor changes into
  feature branches. Each feature branch must contain exactly one commit.
- For every merge, record the branch, commit SHA, Debug build result, and full
  correctness test result. Merge in dependency order using fast-forward only.
- Finish on local `main` with a clean working tree. Do not rewrite unrelated
  repository history to make the branch-count metric appear to pass; record
  incompatible pre-existing history as a blocker if it cannot be reconciled
  without losing work.

## 2. Establish a reproducible Debug baseline

- Configure a new x64 Debug build directory from the checked-in CMake preset or
  the documented equivalent.
- Build the editor, sandbox, core library, and graphics backend.
- Require zero errors and zero project-owned compiler warnings.
- Run every registered unit and integration test after each feature branch and
  again after the final merge.
- Save configure, build, and test logs. The clean-directory workflow in
  `docs/testing.md` must reproduce the result.

## 3. Close behavioral test gaps

Each required area needs success and failure or edge-case tests:

- **Transforms and hierarchy:** parent composition, cache invalidation,
  reparenting, cycle rejection, degenerate transforms, and stable ownership.
- **Persistence and migrations:** round trips, schema upgrades, unknown fields,
  malformed fields, invalid values, and failed atomic saves.
- **Asset loading:** supported formats, generated attributes, material
  extensions, malformed accessors, missing dependencies, and importer failures.
- **Events:** matching and nonmatching dispatch, handled-state propagation, and
  safe callback behavior.
- **Renderer lifecycle:** invalid device/setup, resource upload, frame lifecycle,
  resize, and recoverable upload/render failures. Use the WARP integration path
  where a real backend is required.
- **Editor command history:** execute, undo, redo, invalid commands, empty
  history, redo invalidation, and command ownership.

The full registered test suite must pass on each feature branch and on final
`main`. Keep the CTest log for each gate.

## 4. Meet core coverage thresholds

- Instrument core production C++ with clang-cl source coverage.
- Exclude tests, generated code, vendored code, shader output, and build trees.
- Require at least 80% line coverage and 70% branch coverage.
- Publish both the readable `llvm-cov report` text and machine-readable export,
  along with the merged profile and exact filters used.
- Generate the final coverage report from the complete correctness suite on
  final `main`; intermediate reports are progress only.

## 5. Record repeatable CPU benchmarks

Maintain a repeatable benchmark for each important CPU path:

- transform world-matrix evaluation and parent-chain depth;
- scene traversal at representative entity counts;
- asset import or scene-document processing;
- render-graph construction and compilation.

Save benchmark JSON and metadata for compiler, build configuration, commit,
operating system, CPU, benchmark version, and machine conditions. Benchmarks are
reported for comparison and are not a performance merge gate.

## 6. Verify architecture and code quality

- Scan all public core headers: none may expose NVRHI, D3D11, Win32, GLFW, or
  ImGui types.
- Scan editor code: no backend downcasts or native graphics-resource access may
  exist outside backend integration.
- Review every logged dead-code candidate and record a disposition. Remove
  confirmed unused project-owned symbols and dependencies, or document the
  specific reason each retained candidate is required.
- Document every public core declaration and run Doxygen with warnings treated
  as errors; require zero warnings.
- Run formatting and clang-tidy with zero actionable project diagnostics.
- Save architecture scans, candidate dispositions, Doxygen output, formatting
  output, and clang-tidy output.

## 7. Final audit and completion record

On final local `main`, run the clean x64 Debug configure/build/test workflow,
coverage, Doxygen, formatting, clang-tidy, architecture scans, and benchmark
smoke checks. Record a metric table with pass/fail, command, and artifact path.
Leave the tree clean. Do not declare completion while any metric is failing,
unmeasured, or missing its report.

## Current integration checkpoint

The ECS renderer and scene rewrite is being integrated on
`codex/refactor-completion`. The ECS scene, editor hierarchy and inspector,
command history, renderer, and Doxygen setup are present. The old object and
transform scene API and its dependent sources and tests have been removed.

The preset-equivalent clean x64 Debug directory is `out/build/ecs-x64-debug`.
The full build completed with no project-owned compiler warnings or errors. The
registered suite passed 41/41 tests after updating the unsupported-schema test
to use version 3 (version 2 is current). Logs are saved under
`artifacts/refactor/ecs-x64-debug-*`.

Doxygen generated HTML, but the configured warning-as-error run fails. The
warning scan reports 283 undocumented public declarations or missing parameter
descriptions. Its report is saved under `artifacts/refactor/doxygen-*`. Public
declaration documentation is still an open gate.

The current benchmark measures ECS archetype column iteration only. Required
transform/hierarchy, scene or asset processing, and render-graph workloads are
not yet present. Final ECS coverage has not been collected. Formatting,
clang-tidy, dead-code dispositions, and the complete final-main architecture and
history audits remain open. Do not treat these branch-level build and test
results as a final-main completion report.
