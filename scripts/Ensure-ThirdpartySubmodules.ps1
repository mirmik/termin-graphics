param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string[]]$RequiredPaths = @()
)

$ErrorActionPreference = "Stop"

$normalizedPaths = @(
    foreach ($path in $RequiredPaths) {
        if ($path) {
            $path.Replace("\", "/")
        }
    }
) | Select-Object -Unique

if ($normalizedPaths.Count -eq 0) {
    return
}

$pythonCommand = Get-Command python -ErrorAction SilentlyContinue
if (-not $pythonCommand) {
    $pythonCommand = Get-Command python3 -ErrorAction SilentlyContinue
}
if (-not $pythonCommand) {
    throw "Python executable not found; cannot initialize required submodules"
}

$oldPythonPath = $env:PYTHONPATH
$buildToolsPath = Join-Path $RepoRoot "termin-build-tools"
$separator = [System.IO.Path]::PathSeparator
if ($oldPythonPath) {
    $env:PYTHONPATH = "$buildToolsPath$separator$oldPythonPath"
} else {
    $env:PYTHONPATH = $buildToolsPath
}

try {
    & $pythonCommand.Source -m termin_build.sdk --repo-root $RepoRoot ensure-submodules @normalizedPaths
    if ($LASTEXITCODE -ne 0) {
        throw "required submodule initialization failed"
    }
}
finally {
    $env:PYTHONPATH = $oldPythonPath
}
