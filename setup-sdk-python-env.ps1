#!/usr/bin/env pwsh
# Create checkout-local test tooling and a source overlay over bundled SDK Python.

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Force = $false

foreach ($arg in $args) {
    if ($arg -eq "--force" -or $arg -eq "-f") {
        $Force = $true
    } elseif ($arg -eq "--help" -or $arg -eq "-h") {
        Write-Host "Usage: .\setup-sdk-python-env.ps1 [--force]"
        exit 0
    } else {
        throw "Unknown argument: $arg"
    }
}

$SdkRoot = if ($env:TERMIN_SDK) { $env:TERMIN_SDK } else { Join-Path $ScriptDir "sdk" }
$SdkPython = Join-Path $SdkRoot "bin\termin_python.exe"
$EnvRoot = if ($env:TERMIN_TEST_ENV) { $env:TERMIN_TEST_ENV } else { Join-Path $ScriptDir "build\python-envs\test" }
$ToolsSite = Join-Path $EnvRoot "site-packages"
$ToolsRequirements = Join-Path $ScriptDir "build-system\python-test-requirements.txt"
$ToolsStamp = Join-Path $EnvRoot "python-test-requirements.txt"
$OverlayManifest = Join-Path $EnvRoot "overlay.json"
$BuildToolsRoot = Join-Path $ScriptDir "termin-build-tools"

if (-not (Test-Path $SdkPython -PathType Leaf)) {
    throw "Isolated SDK Python launcher is missing: $SdkPython. Run .\build-sdk.ps1 --no-wheels first."
}

$BootstrapPython = if ($env:PYTHON_BOOTSTRAP) { $env:PYTHON_BOOTSTRAP } else { "python" }
if ($Force -and (Test-Path $ToolsSite)) {
    Remove-Item -Recurse -Force $ToolsSite
}
New-Item -ItemType Directory -Force -Path $ToolsSite | Out-Null

$ToolsCurrent = (Test-Path (Join-Path $ToolsSite "ruff")) -and `
    (Test-Path $ToolsStamp) -and `
    ((Get-FileHash $ToolsRequirements).Hash -eq (Get-FileHash $ToolsStamp).Hash)
if ($Force -or -not $ToolsCurrent) {
    Write-Host "Installing test-only tools into: $ToolsSite"
    & $BootstrapPython -I -m pip install `
        --no-deps `
        --ignore-installed `
        --upgrade `
        --target $ToolsSite `
        -r $ToolsRequirements
    if ($LASTEXITCODE -ne 0) { throw "test tool installation failed" }
    Copy-Item -Force $ToolsRequirements $ToolsStamp
} else {
    Write-Host "Test-only tools are up to date: $ToolsSite"
}

Write-Host "Generating checkout overlay: $OverlayManifest"
$OverlayBootstrap = "import sys; sys.path.insert(0, sys.argv.pop(1)); from termin_build.python_overlay import main; raise SystemExit(main())"
& $SdkPython -c $OverlayBootstrap $BuildToolsRoot `
    --repo-root $ScriptDir `
    --sdk-root $SdkRoot `
    --output $OverlayManifest `
    --extra-site $ToolsSite
if ($LASTEXITCODE -ne 0) { throw "Python overlay generation failed" }

Write-Host "SDK-backed Python test environment is ready."
