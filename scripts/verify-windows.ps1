param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "Run this script from a Visual Studio Developer PowerShell with the x64 C++ workload enabled."
}

$preset = "x64-$($Configuration.ToLowerInvariant())"
$buildDirectory = "out/build/$preset"

cmake --preset $preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $buildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --test-dir $buildDirectory --build-config $Configuration --output-on-failure
exit $LASTEXITCODE
