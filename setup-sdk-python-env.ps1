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
$OverlayManifest = Join-Path $EnvRoot "overlay.json"
$BuildToolsRoot = Join-Path $ScriptDir "termin-build-tools"
$PythonBuildEnv = if ($env:TERMIN_PYTHON_BUILD_ENV) {
    $env:TERMIN_PYTHON_BUILD_ENV
} else {
    Join-Path $ScriptDir "build\python-runtime\build-env"
}
$TestToolsPython = if ($env:TERMIN_TEST_TOOLS_PYTHON) {
    $env:TERMIN_TEST_TOOLS_PYTHON
} else {
    Join-Path $PythonBuildEnv "Scripts\python.exe"
}

if (-not (Test-Path $SdkPython -PathType Leaf)) {
    throw "Isolated SDK Python launcher is missing: $SdkPython. Run .\build-sdk.ps1 --no-wheels first."
}

if (-not (Test-Path $TestToolsPython -PathType Leaf)) {
    throw "Pinned SDK Python build frontend is missing: $TestToolsPython. Run .\build-sdk.ps1 --no-wheels first."
}
$EnvironmentBootstrap = "import sys; sys.path.insert(0, sys.argv.pop(1)); from termin_build.python_test_environment import main; raise SystemExit(main())"
$PrepareArgs = @(
    "prepare",
    "--environment-root", $EnvRoot,
    "--requirements", $ToolsRequirements,
    "--installer-python", $TestToolsPython
)
if ($Force) {
    $PrepareArgs += "--force"
}
& $SdkPython -c $EnvironmentBootstrap $BuildToolsRoot @PrepareArgs
if ($LASTEXITCODE -ne 0) { throw "Python test environment preparation failed" }

Write-Host "Generating checkout overlay: $OverlayManifest"
$OverlayBootstrap = "import sys; sys.path.insert(0, sys.argv.pop(1)); from termin_build.python_overlay import main; raise SystemExit(main())"
& $SdkPython -c $OverlayBootstrap $BuildToolsRoot `
    --repo-root $ScriptDir `
    --sdk-root $SdkRoot `
    --output $OverlayManifest `
    --extra-site $ToolsSite
if ($LASTEXITCODE -ne 0) { throw "Python overlay generation failed" }

Write-Host "SDK-backed Python test environment is ready."
