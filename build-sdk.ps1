#!/usr/bin/env pwsh
# Build the SDK through the shared Python orchestrator.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

$pythonCommand = $null
if ($env:PYTHON_BIN) {
    $pythonCommand = Get-Command $env:PYTHON_BIN -ErrorAction SilentlyContinue
}
if (-not $pythonCommand -and $env:PYTHON_EXECUTABLE) {
    $pythonCommand = Get-Command $env:PYTHON_EXECUTABLE -ErrorAction SilentlyContinue
}
if (-not $pythonCommand) {
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
}
if (-not $pythonCommand) {
    $pythonCommand = Get-Command python3 -ErrorAction SilentlyContinue
}
if (-not $pythonCommand) {
    throw "Python executable not found in PATH"
}

$orchestratorArgs = @($args)
$hasWheelFlag = $false
foreach ($arg in $orchestratorArgs) {
    if ($arg -eq "--no-wheels" -or $arg -eq "--wheels") {
        $hasWheelFlag = $true
        break
    }
}
if (-not $hasWheelFlag) {
    $orchestratorArgs += "--no-wheels"
}

$oldPythonPath = $env:PYTHONPATH
$env:PYTHONPATH = (Join-Path $ScriptDir "termin-build-tools")
if ($oldPythonPath) {
    $env:PYTHONPATH = "$env:PYTHONPATH$([IO.Path]::PathSeparator)$oldPythonPath"
}

& $pythonCommand.Source -m termin_build.sdk --repo-root $ScriptDir build @orchestratorArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
