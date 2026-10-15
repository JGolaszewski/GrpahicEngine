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

    Write-Host 'Checking Core API documentation with strict Doxygen settings.'
    Remove-GeneratedDirectory $doxygenOutput
    Invoke-CheckedCommand $doxygenCommand.Source @('Doxyfile')

    Write-Host 'Building the strict MkDocs site and generating the Core API from Doxygen XML.'
    Invoke-CheckedCommand $mkdocsCommand @('build', '--strict', '--clean')

    Write-Host "Documentation site ready at $siteOutput"
    if ($Serve) {
        Invoke-CheckedCommand $mkdocsCommand @('serve', '--strict')
    }
}
finally {
    Pop-Location
}
