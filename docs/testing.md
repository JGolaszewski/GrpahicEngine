# Testing and performance workflow

## Windows x64 Debug environment

The preset workflow targets Windows 11, Visual Studio 2022 or newer, CMake 3.28+, and Ninja. Run it from Visual Studio Developer PowerShell. The `x64-debug` preset expects `VCPKG_ROOT` and `clang-tidy`; shader compilation also requires the Windows SDK `fxc` compiler. The root `CMakeLists.txt` declares a lower CMake minimum, which does not validate the preset workflow with older CMake releases.

The CMake export is `out/build/x64-debug/compile_commands.json`. Copy it to the repository root only when a tool specifically requires that location:

```powershell
Copy-Item out/build/x64-debug/compile_commands.json compile_commands.json -Force
```

The standard configure, build, and CTest commands are listed below. To remove generated build outputs, run `cmake --build out/build/x64-debug --target clean`.

## Codex edit hooks

After `apply_patch`, `Edit`, or `Write`, the Python hook filters changed C++ files
to first-party files inside the repository. It exits without work when no eligible
files changed or `out/build/x64-debug/CMakeCache.txt` is absent. It formats only
those files when `clang-format` is available, incrementally builds the existing
x64 Debug tree, invokes the CMake `lint` target for clang-tidy, and runs the
`unit` and `integration` CTest labels while excluding `benchmark`. On Windows
Ninja, `lint` is a separate target and analyzes the configured first-party
source set. Build, lint, and test failures block the edit and include the tail
of captured output; formatter errors and a missing formatter do not block.

The hooks use the environment inherited from Codex and do not initialize the
Visual Studio developer environment. Run Codex from an environment where the
existing Ninja/MSVC build can compile. The edit hook has a 300-second timeout.

After an explicit Bash command containing `ctest`, a separate hook configures,
builds, and runs the complete `x64-asan` test tree. Commands already targeting
`out/build/x64-asan` are ignored by that hook.

## Correctness tests

### When to add tests

Add a focused regression test for every bug fix and tests for new deterministic
behavior, boundary conditions, and failure results. Prefer tests of observable
contracts over tests coupled to private implementation details. Keep unit tests
fast and independent of windows, GPU devices, external services, and unnecessary
filesystem state. Use integration tests when real subsystem or platform
interaction is required. Before adding a test, inspect the current checkout's
CMake test registration and `ctest -N` output; those are authoritative for the
available framework, target names, and labels. In the current checkout, GoogleTest
unit tests live under `GraphicEngineCore/tests/unit/` in the
`GraphicEngineUnitTests` target; `BrdfReferenceTests` is also labeled `unit`,
and `AssetManagerTests` is labeled `integration`.

### Running focused and full checks

Configure and build the relevant test target, then use CTest to run a focused
test selection. Run the complete registered suite before review when the
toolchain permits:

```powershell
ctest --test-dir out/build/x64-debug -N
ctest --test-dir out/build/x64-debug --output-on-failure -C Debug -R "<test-name-or-pattern>"
ctest --test-dir out/build/x64-debug --output-on-failure -C Debug -L unit
ctest --test-dir out/build/x64-debug --output-on-failure -C Debug
```

Run `ctest --test-dir out/build/x64-debug --output-on-failure -C Debug -L integration`
for the integration subset. The `engine-unit-tests`,
`engine-integration-tests`, and `engine-tests` build targets also build and run
the corresponding CTest selections.

Ninja builds also run the repository's `lint` target during compilation.
`clang-tidy` is required and its configured findings can fail the build, so a
successful build includes the clang-tidy gate. Fix applicable first-party
findings rather than suppressing or bypassing the check. If the toolchain is
unavailable, report which checks could not run. Makefile builds also run `lint`;
other generators attach clang-tidy to first-party C++ compilation.

The default x64 Debug preset builds the asset and BRDF executables and the GoogleTest
CPU unit suite. Tests use temporary project roots where they write scene or asset
files; the suite does not create a window or graphics device.

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug --parallel $env:NUMBER_OF_PROCESSORS
ctest --test-dir out/build/x64-debug --output-on-failure -C Debug -L "unit|integration"
```

Run all correctness tests or select a GoogleTest case with:

```powershell
ctest --test-dir out/build/x64-debug --output-on-failure -C Debug
out/build/x64-debug/bin/GraphicEngineUnitTests.exe --gtest_filter="ArchetypeWorldTests.*"
ctest --test-dir out/build/x64-debug --output-on-failure -R "AssetManagerTests|BrdfReferenceTests"
```

CTest discovers the individual GoogleTest cases at test time, so `-R` and
`--gtest_filter` can narrow runs. `AssetManagerTests` is the integration executable;
`BrdfReferenceTests` is a standalone numerical reference suite. New tests belong
under `tests/unit/<domain>`.

## Benchmarks

Benchmarks use a separate optional vcpkg feature. They are not built or run by the
normal correctness workflow. Configure a Release tree with the feature installed,
then run the benchmark executable directly to write results to a caller-selected
path:

```powershell
cmake -S . -B out/build/x64-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_MANIFEST_FEATURES=benchmarks `
  -DENGINE_BUILD_BENCHMARKS=ON
cmake --build out/build/x64-release --target GraphicEngineBenchmarks --parallel $env:NUMBER_OF_PROCESSORS
out/build/x64-release/bin/GraphicEngineBenchmarks.exe `
  --benchmark_out=out/build/x64-release/transform-benchmarks.json `
  --benchmark_out_format=json
```

The current benchmark target measures ECS archetype column iteration at several
entity counts. Google Benchmark handles warmup, repeated samples, statistics, and
JSON output. Transform/hierarchy, scene or asset processing, and render-graph
benchmarks remain open work. Compare only Release runs from controlled
hardware; record the commit, compiler, OS, CPU, benchmark version, power mode,
and background load. Clock scaling, thermal throttling, and fixture setup affect
results. There are no timing gates in CTest. To include the registered smoke run in CTest, use
`ctest -L benchmark`; it writes `benchmarks.json` under that build directory.

The manifest's `benchmarks` feature adds Google Benchmark only when explicitly
requested, keeping it out of ordinary vcpkg installs.

## Source coverage

MSVC `/PROFILE` is sampling instrumentation, not source coverage. For LLVM source
coverage, use a separate clang-cl build and compatible LLVM runtime/tools. Keep
coverage instrumentation enabled for both engine objects and test binaries:

```powershell
New-Item -ItemType Directory -Force out/coverage | Out-Null
$env:LLVM_PROFILE_FILE = "${PWD}/out/coverage/run-%p.profraw"
cmake -S . -B out/build/coverage -G Ninja `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" `
  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping -Wno-unused-command-line-argument" `
  -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate" `
  -DCMAKE_SHARED_LINKER_FLAGS="-fprofile-instr-generate"
cmake --build out/build/coverage --parallel $env:NUMBER_OF_PROCESSORS
ctest --test-dir out/build/coverage --output-on-failure
llvm-profdata merge -sparse out/coverage/*.profraw -o out/coverage/merged.profdata
llvm-cov report out/build/coverage/bin/GraphicEngineCore.dll `
  -instr-profile=out/coverage/merged.profdata `
  -ignore-filename-regex="(vendor|out[/\\]build|tests|shaders|public_header_checks)" `
  -include-filename-regex="GraphicEngineCore.*src.*\.cpp$"
llvm-cov export out/build/coverage/bin/GraphicEngineCore.dll `
  -instr-profile=out/coverage/merged.profdata `
  -ignore-filename-regex="(vendor|out[/\\]build|tests|shaders|public_header_checks)" `
  -include-filename-regex="GraphicEngineCore.*src.*\.cpp$" `
  > out/coverage/core-coverage.json
```

Use `llvm-cov export` with the same source filters to publish a machine-readable
report beside the text summary. Count core production C++ only:
exclude tests, vendored code, generated/build files, compiled shader output, and
temporary fixture trees. Report line and branch coverage. GPU-only paths and
Windows driver behavior are outside CPU coverage. The preset currently expects
MSVC, so coverage configuration is intentionally separate and must be validated
with the installed clang-cl, LLVM runtime, and vcpkg triplet before relying on its
results.

## Core API documentation

Install Doxygen and generate the core API reference from the checked-in
configuration. Undocumented public declarations and documentation errors fail
the run:

```powershell
doxygen docs/Doxyfile
```

HTML is written to `out/build/doxygen/html`; the warning log is written to
`out/build/doxygen-warnings.log`.

## CI guidance

Run Windows MSVC x64 Debug configure/build and `unit|integration` CTest labels on
every pull request. Use a controlled optimized runner for benchmark baselines,
optional clang-cl for coverage, and a dedicated Windows GPU runner only for future
GPU integration tests. Hosted GPU availability is not a correctness prerequisite.
