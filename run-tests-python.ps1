#!/usr/bin/env pwsh
# Run Python test suites across projects.
#
# Uses the isolated bundled SDK Python plus a checkout-local source overlay.
#
# Usage:
#   .\run-tests-python.ps1
#   .\run-tests-python.ps1 --full
#   .\run-tests-python.ps1 termin-app/tests/test_project_file_watcher.py -q
# Selected pytest-target runs skip the repo-wide Python lint suite.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PytestTargets = New-Object System.Collections.Generic.List[string]
$Full = $false

function Show-Help {
    Write-Host "Usage: .\run-tests-python.ps1 [pytest-target ...]"
    Write-Host ""
    Write-Host "  (no flags)  Use SDK Python + checkout overlay and run working tests"
    Write-Host "  --full      Include pytest tests marked full"
    Write-Host "  pytest-target"
    Write-Host "              Run only selected pytest target(s), e.g. termin-app/tests/test_game_mode_model.py"
    Write-Host "              Selected runs skip the repo-wide Python lint suite."
}

foreach ($arg in $args) {
    if ($arg -eq "--no-venv") {
        Write-Error "--no-venv is no longer supported; run .\setup-sdk-python-env.ps1 first."
        exit 1
    } elseif ($arg -eq "--full") {
        $Full = $true
    } elseif ($arg -eq "--help" -or $arg -eq "-h") {
        Show-Help
        exit 0
    } elseif ($arg.StartsWith("--")) {
        Write-Error "Unknown option: $arg"
        exit 1
    } else {
        $PytestTargets.Add($arg)
    }
}

if (-not $env:TERMIN_SDK) {
    $localSdk = Join-Path $ScriptDir "sdk"
    if (Test-Path (Join-Path $localSdk "lib")) {
        $env:TERMIN_SDK = $localSdk
    } else {
        $localAppDataSdk = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA "termin-sdk" } else { "" }
        if ($localAppDataSdk -and (Test-Path (Join-Path $localAppDataSdk "lib"))) {
            $env:TERMIN_SDK = $localAppDataSdk
        } elseif (Test-Path "/opt/termin/lib") {
            $env:TERMIN_SDK = "/opt/termin"
        }
    }
}
if ($env:TERMIN_SDK) {
    Write-Host "TERMIN_SDK: $($env:TERMIN_SDK)"
} else {
    throw "Termin SDK was not found."
}

$PythonBin = if ($env:PYTHON_BIN) { $env:PYTHON_BIN } else { Join-Path $env:TERMIN_SDK "bin\termin_python.exe" }
$OverlayManifest = if ($env:TERMIN_PYTHON_OVERLAY) { $env:TERMIN_PYTHON_OVERLAY } else { Join-Path $ScriptDir "build\python-envs\test\overlay.json" }
if (-not (Test-Path $PythonBin -PathType Leaf)) {
    throw "SDK Python launcher is missing: $PythonBin"
}
if (-not (Test-Path $OverlayManifest -PathType Leaf)) {
    throw "Python test overlay is missing: $OverlayManifest. Run .\setup-sdk-python-env.ps1 first."
}
$PythonPrefixArgs = @("--termin-overlay", $OverlayManifest)
Write-Host "Python: $PythonBin"
Write-Host "Overlay: $OverlayManifest"

$SdkPrefix = if ($env:SDK_PREFIX) { $env:SDK_PREFIX } else { $env:TERMIN_SDK }

$pathEntries = @(
    (Join-Path $SdkPrefix "bin"),
    (Join-Path $SdkPrefix "lib")
) | Where-Object { Test-Path $_ }
if ($pathEntries.Count -gt 0) {
    $env:PATH = ($pathEntries -join [IO.Path]::PathSeparator) + [IO.Path]::PathSeparator + $env:PATH
}

$sdkLib = Join-Path $SdkPrefix "lib"
if ($env:LD_LIBRARY_PATH) {
    $env:LD_LIBRARY_PATH = "$sdkLib$([IO.Path]::PathSeparator)$($env:LD_LIBRARY_PATH)"
} else {
    $env:LD_LIBRARY_PATH = $sdkLib
}

Remove-Item Env:PYTHONHOME -ErrorAction SilentlyContinue
Remove-Item Env:PYTHONPATH -ErrorAction SilentlyContinue
Remove-Item Env:PYTHONUSERBASE -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "========================================"
if ($Full) {
    Write-Host "  Python tests (full)"
} else {
    Write-Host "  Python tests (working set)"
}
Write-Host "========================================"

Set-Location $ScriptDir
$Failures = New-Object System.Collections.Generic.List[string]

$PytestTempRoot = Join-Path (Join-Path $ScriptDir "build") "pytest-temp"
$PytestCacheRoot = Join-Path (Join-Path $ScriptDir "build") "pytest-cache"
$PytestRunTempDir = Join-Path $PytestTempRoot ([System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $PytestRunTempDir -Force | Out-Null
New-Item -ItemType Directory -Path $PytestCacheRoot -Force | Out-Null
$env:TEMP = $PytestRunTempDir
$env:TMP = $PytestRunTempDir
Write-Host "Pytest temp root: $PytestRunTempDir"

$PytestMarkerArgs = @()
if (-not $Full) {
    $PytestMarkerArgs = @("-m", "not full")
}

function New-PytestSuiteArgs {
    param([string]$Name)

    $safeName = $Name -replace "[^A-Za-z0-9_.-]", "-"
    $suiteTempDir = Join-Path $PytestRunTempDir $safeName
    $suiteCacheDir = Join-Path $PytestCacheRoot $safeName
    New-Item -ItemType Directory -Path $suiteTempDir -Force | Out-Null
    New-Item -ItemType Directory -Path $suiteCacheDir -Force | Out-Null
    return @("--basetemp", $suiteTempDir, "-o", "cache_dir=$suiteCacheDir")
}

function Invoke-TestSuite {
    param(
        [string]$Name,
        [string[]]$CommandArgs
    )

    Write-Host ""
    Write-Host "----------------------------------------"
    Write-Host "  $Name"
    Write-Host "----------------------------------------"

    & $PythonBin @PythonPrefixArgs @CommandArgs
    if ($LASTEXITCODE -ne 0) {
        $Failures.Add($Name)
    }
}

if ($PytestTargets.Count -gt 0) {
    Invoke-TestSuite "selected python" (@("-m", "pytest") + $PytestMarkerArgs + $PytestTargets.ToArray() + (New-PytestSuiteArgs "selected-python") + @("-v"))
} else {
    $TestProfile = if ($Full) { "windows-d3d11" } else { "pr" }
    & $PythonBin @PythonPrefixArgs -m termin_build.repository_control `
        --repo-root $ScriptDir run $TestProfile `
        --platform windows --python $PythonBin `
        --python-arg=--termin-overlay --python-arg=$OverlayManifest
    if ($LASTEXITCODE -ne 0) {
        $Failures.Add("manifest Python suites")
    }

    Invoke-TestSuite "termin-modules import smoke" @("-c", "import termin_modules; env = termin_modules.ModuleEnvironment(); runtime = termin_modules.ModuleRuntime(); runtime.set_environment(env); runtime.register_cpp_backend(termin_modules.CppModuleBackend()); runtime.register_python_backend(termin_modules.PythonModuleBackend())")
    Invoke-TestSuite "Python lint" @("-m", "ruff", "check", $ScriptDir)
}

if ($Failures.Count -gt 0) {
    Write-Host ""
    Write-Host "========================================"
    Write-Host "  Python test failures"
    Write-Host "========================================"
    foreach ($failure in $Failures) {
        Write-Host "  - $failure"
    }
    exit 1
}

Write-Host ""
Write-Host "========================================"
Write-Host "  Python tests finished"
Write-Host "========================================"
