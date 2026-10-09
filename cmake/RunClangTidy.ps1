param(
    [Parameter(Mandatory = $true)]
    [string]$CompilationDatabase,

    [Parameter(Mandatory = $true)]
    [string]$ClangTidy,

    [Parameter(Mandatory = $true)]
    [string]$Sources,

    [switch]$UseClDriver
)

$ErrorActionPreference = 'Stop'

$databasePath = (Resolve-Path -LiteralPath $CompilationDatabase).Path
$buildDirectory = Split-Path -Parent $databasePath
$tidyDatabaseDirectory = Join-Path $buildDirectory 'clang-tidy-database'
$tidyDatabasePath = Join-Path $tidyDatabaseDirectory 'compile_commands.json'

New-Item -ItemType Directory -Path $tidyDatabaseDirectory -Force | Out-Null
$json = Get-Content -LiteralPath $databasePath -Raw
# These MSVC-only options are either unsupported by clang-cl or make ordinary
# clang diagnostics errors; clang-tidy applies the project's analysis checks.
$json = [regex]::Replace(
    $json,
    '(?<=\s)(?:/analyze|/WX|-ZI)(?=\s)',
    ''
)
[System.IO.File]::WriteAllText(
    $tidyDatabasePath,
    $json,
    [System.Text.UTF8Encoding]::new($false)
)

$clangTidyArguments = @('--quiet', '-p', $tidyDatabaseDirectory)
if ($UseClDriver) {
    $clangTidyArguments = @('--extra-arg-before=--driver-mode=cl') + $clangTidyArguments
}
$sourceList = @($Sources -split ';' | Where-Object { $_ })
$clangTidyArguments += $sourceList

& $ClangTidy @clangTidyArguments
exit $LASTEXITCODE
