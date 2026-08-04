#!/usr/bin/env pwsh
# Build the SDK through the shared Python orchestrator.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "scripts\Normalize-WindowsSdkPermissions.ps1")

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

$oldPythonPath = $env:PYTHONPATH
$env:PYTHONPATH = (Join-Path $ScriptDir "termin-build-tools")
if ($oldPythonPath) {
    $env:PYTHONPATH = "$env:PYTHONPATH$([IO.Path]::PathSeparator)$oldPythonPath"
}

& $pythonCommand.Source -m termin_build.sdk --repo-root $ScriptDir build @orchestratorArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$sdkPrefix = if ($env:SDK_PREFIX) {
    $env:SDK_PREFIX
} else {
    Join-Path $ScriptDir "sdk"
}
Enable-TerminSdkInheritedPermissions -SdkPrefix $sdkPrefix

$launcherPath = Join-Path $sdkPrefix "bin\termin_launcher.exe"
if (-not (Test-Path $launcherPath -PathType Leaf)) {
    throw "SDK launcher is missing after build: $launcherPath"
}
Write-Host "+ verify launcher bundled Python layout"
& $launcherPath --termin-python-layout-smoke
if ($LASTEXITCODE -ne 0) {
    throw "SDK launcher bundled Python layout smoke failed with exit code $LASTEXITCODE"
}
