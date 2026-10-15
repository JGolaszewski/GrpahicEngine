# Testing and performance workflow

## Correctness tests

The default x64 Debug preset builds the existing asset and BRDF executables and the
GoogleTest CPU unit suite. Unit tests do not create a window or graphics device.
Asset tests use temporary project roots and clean them up when the test exits. The
registered correctness suite includes CPU tests and a D3D11 WARP software-device
integration path for renderer lifecycle and application scene load/save behavior.

```powershell
cmake --preset x64-debug
cmake --build out/build/x64-debug --parallel $env:NUMBER_OF_PROCESSORS
ctest --test-dir out/build/x64-debug --output-on-failure -C Debug -L "unit|integration"
```

Run all correctness tests or select a GoogleTest case with:

```powershell
ctest --test-dir out/build/x64-debug --output-on-failure -C Debug
out/build/x64-debug/bin/GraphicEngineUnitTests.exe --gtest_filter="TransformTests.*"
ctest --test-dir out/build/x64-debug --output-on-failure -R "AssetManagerTests|BrdfReferenceTests"
```

CTest discovers the individual GoogleTest cases at test time, so `-R` and
`--gtest_filter` can narrow runs. `AssetManagerTests` remains an integration-style
legacy executable while it is incrementally migrated; `BrdfReferenceTests` remains
a standalone numerical reference suite. New tests belong under `tests/unit/<domain>`.
Slow numerical integration should use a `slow` label and stay outside the default
PR run. The WARP tests explicitly create and own a software D3D11 device, so they
do not require a physical GPU.

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

The benchmark target measures transform world-matrix cache hits and parent-chain
depth, scene traversal at several entity counts, scene-document saving, and
render-graph build and compile work. Google Benchmark handles warmup, repeated
samples, statistics, and JSON output. Compare only Release runs from controlled
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
