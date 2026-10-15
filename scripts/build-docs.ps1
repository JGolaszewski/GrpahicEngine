[CmdletBinding()]
param(
    [switch]$Serve
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = Join-Path $repoRoot 'out\docs'
$doxygenOutput = Join-Path $buildRoot 'doxygen'
$siteOutput = Join-Path $buildRoot 'site'
$venvPath = Join-Path $repoRoot '.venv-docs'

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Command $($Arguments -join ' ')"
    }
}

function Remove-GeneratedDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)

    $allowedRoot = [System.IO.Path]::GetFullPath($buildRoot).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a path outside the generated docs directory: $fullPath"
    }
    if (Test-Path -LiteralPath $fullPath) {
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
}

Push-Location $repoRoot
try {
    $pythonCommand = Get-Command python -ErrorAction Stop
    $doxygenCommand = Get-Command doxygen -ErrorAction Stop
    Write-Host "Python: $(& $pythonCommand.Source --version 2>&1)"
    Write-Host "Doxygen: $(& $doxygenCommand.Source --version)"

    if (-not (Test-Path -LiteralPath $venvPath)) {
        Write-Host 'Creating the project documentation virtual environment.'
        Invoke-CheckedCommand $pythonCommand.Source @('-m', 'venv', $venvPath)
    }

    if ($IsWindows -or $env:OS -eq 'Windows_NT') {
        $venvScripts = Join-Path $venvPath 'Scripts'
        $venvPython = Join-Path $venvScripts 'python.exe'
        $mkdocsCommand = Join-Path $venvScripts 'mkdocs.exe'
    }
    else {
        $venvScripts = Join-Path $venvPath 'bin'
        $venvPython = Join-Path $venvScripts 'python'
        $mkdocsCommand = Join-Path $venvScripts 'mkdocs'
    }

    Write-Host 'Installing pinned documentation packages into .venv-docs.'
    Invoke-CheckedCommand $venvPython @('-m', 'pip', 'install', '--disable-pip-version-check', '-r', 'requirements-docs.txt')

    New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
    Remove-GeneratedDirectory $doxygenOutput

    Write-Host 'Generating GraphicEngineCore API reference with Doxygen.'
    Invoke-CheckedCommand $doxygenCommand.Source @('Doxyfile')

    Write-Host 'Building the strict MkDocs site.'
    Invoke-CheckedCommand $mkdocsCommand @('build', '--strict', '--clean')

    $doxygenHtml = Join-Path $doxygenOutput 'html'
    $apiOutput = Join-Path $siteOutput 'api'
    if (-not (Test-Path -LiteralPath (Join-Path $doxygenHtml 'index.html'))) {
        throw "Doxygen did not produce the expected HTML entry point: $doxygenHtml"
    }
    New-Item -ItemType Directory -Path $apiOutput -Force | Out-Null
    Copy-Item -Path (Join-Path $doxygenHtml '*') -Destination $apiOutput -Recurse -Force

    Write-Host "Documentation site ready at $siteOutput"
    if ($Serve) {
        Invoke-CheckedCommand $mkdocsCommand @('serve', '--strict')
    }
}
finally {
    Pop-Location
}
