[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EditorPath,
    [string]$EditorArguments = ""
)

$resolvedEditor = (Resolve-Path -LiteralPath $EditorPath -ErrorAction Stop).Path
$reportDirectory = Join-Path $env:TEMP "GraphicEngine/GpuDiagnosticRuns"
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$runId = [Guid]::NewGuid().ToString("N")
$logPath = Join-Path $reportDirectory "$runId.jsonl"
$reportPath = Join-Path $reportDirectory "$runId.report.json"

$previousLogPath = $env:ENGINE_LOG_PATH
$previousBreadcrumbPath = $env:ENGINE_LOG_BREADCRUMB_PATH
$env:ENGINE_LOG_PATH = $logPath
$env:ENGINE_LOG_BREADCRUMB_PATH = Join-Path $reportDirectory "$runId.breadcrumbs.log"
$editorExitCode = $null
$launchError = $null

try {
    $startParameters = @{
        FilePath = $resolvedEditor
        WorkingDirectory = (Split-Path -Parent $resolvedEditor)
        PassThru = $true
        Wait = $true
    }
    if (-not [string]::IsNullOrWhiteSpace($EditorArguments)) {
        $startParameters.ArgumentList = $EditorArguments
    }
    $process = Start-Process @startParameters
    $editorExitCode = $process.ExitCode
} catch {
    $launchError = $_.Exception.Message
} finally {
    if ($null -eq $previousLogPath) {
        Remove-Item Env:ENGINE_LOG_PATH -ErrorAction SilentlyContinue
    } else {
        $env:ENGINE_LOG_PATH = $previousLogPath
    }
    if ($null -eq $previousBreadcrumbPath) {
        Remove-Item Env:ENGINE_LOG_BREADCRUMB_PATH -ErrorAction SilentlyContinue
    } else {
        $env:ENGINE_LOG_BREADCRUMB_PATH = $previousBreadcrumbPath
    }
}

$records = [System.Collections.Generic.List[object]]::new()
if (Test-Path -LiteralPath $logPath -PathType Leaf) {
    foreach ($line in [System.IO.File]::ReadLines($logPath)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        try {
            $records.Add(($line | ConvertFrom-Json -ErrorAction Stop))
        } catch {
            $launchError = "Engine log contained invalid JSONL: $($_.Exception.Message)"
        }
    }
}

$nvrhiErrors = @($records | Where-Object {
    $_.error_code -eq "backend.nvrhi.validation_error" -or
    ($_.category -eq "Backend" -and $_.level -eq "Error" -and $_.message -like "NVRHI diagnostic*")
}).Count
$nvrhiWarnings = @($records | Where-Object {
    $_.category -eq "Backend" -and $_.level -eq "Warning" -and $_.message -like "NVRHI diagnostic*"
}).Count

$status = "clean"
if ($launchError) {
    $status = "launch_or_log_error"
} elseif (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
    $status = "no_log_evidence"
} elseif ($null -ne $editorExitCode -and $editorExitCode -ne 0) {
    $status = "editor_exit_nonzero"
} elseif ($nvrhiErrors -gt 0) {
    $status = "validation_errors"
} elseif ($nvrhiWarnings -gt 0) {
    $status = "validation_warnings"
}

$report = [ordered]@{
    schema_version = 1
    status = $status
    created_utc = [DateTime]::UtcNow.ToString("o")
    editor_exit_code = $editorExitCode
    nvrhi_errors = $nvrhiErrors
    nvrhi_warnings = $nvrhiWarnings
    nvrhi_validation_layer_state = "not_verified"
    log_path = $logPath
    launch_or_log_error = $launchError
    visual_scenarios_verified = $false
}
[System.IO.File]::WriteAllText(
    $reportPath,
    ($report | ConvertTo-Json -Depth 5),
    [System.Text.UTF8Encoding]::new($false)
)

Write-Output "GPU diagnostic session: status=$status, NVRHI errors=$nvrhiErrors, warnings=$nvrhiWarnings"
Write-Output "GRAPHICENGINE_GPU_REPORT:$reportPath"
Write-Output "Log retained for local inspection: $logPath"

if ($status -in @("launch_or_log_error", "no_log_evidence", "editor_exit_nonzero", "validation_errors")) {
    exit 1
}
exit 0
