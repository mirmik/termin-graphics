#!/usr/bin/env pwsh
# Run Python test suites across projects.
#
# Auto-activates .venv/ and auto-detects TERMIN_SDK, so no manual setup is
# needed after setting up the editable test environment.
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
    Write-Host "  (no flags)  Activate .venv/, auto-detect TERMIN_SDK, run working tests"
    Write-Host "  --full      Include pytest tests marked full"
    Write-Host "  pytest-target"
    Write-Host "              Run only selected pytest target(s), e.g. termin-app/tests/test_game_mode_model.py"
    Write-Host "              Selected runs skip the repo-wide Python lint suite."
}

foreach ($arg in $args) {
    if ($arg -eq "--no-venv") {
        Write-Error "--no-venv is no longer supported; run .\setup-test-venv.ps1 first."
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

$venvActivateCandidates = @(
    (Join-Path $ScriptDir ".venv\Scripts\Activate.ps1"),
    (Join-Path $ScriptDir ".venv\bin\Activate.ps1")
)
$ActivatedVenv = $false
foreach ($activatePath in $venvActivateCandidates) {
    if (Test-Path $activatePath) {
        Write-Host "Activating venv: $(Join-Path $ScriptDir '.venv')"
        . $activatePath
        $ActivatedVenv = $true
        break
    }
}
if (-not $ActivatedVenv) {
    Write-Error "test .venv is missing. Run .\setup-test-venv.ps1 before .\run-tests-python.ps1."
    exit 1
}

if ($env:PYTHON_BIN) {
    $PythonBin = $env:PYTHON_BIN
} else {
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if (-not $pythonCommand) {
        $pythonCommand = Get-Command py -ErrorAction SilentlyContinue
    }
    if (-not $pythonCommand) {
        Write-Error "python not found"
        exit 1
    }
    $PythonBin = $pythonCommand.Source
}
Write-Host "Python: $PythonBin"

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
}

$SdkPrefix = if ($env:SDK_PREFIX) { $env:SDK_PREFIX } else { Join-Path $ScriptDir "sdk" }

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

# Editable installs in .venv are the source of truth for Python tests. Do not
# prepend SDK site-packages here: stale installed SDK packages can shadow the
# checkout and hide source changes.
$env:PYTHONPATH = if ($env:PYTHONPATH) { $env:PYTHONPATH } else { "" }

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

    & $PythonBin @CommandArgs
    if ($LASTEXITCODE -ne 0) {
        $Failures.Add($Name)
    }
}

if ($PytestTargets.Count -gt 0) {
    Invoke-TestSuite "selected python" (@("-m", "pytest") + $PytestMarkerArgs + $PytestTargets.ToArray() + (New-PytestSuiteArgs "selected-python") + @("-v"))
} else {
    Invoke-TestSuite "Python lint" @("-m", "ruff", "check", $ScriptDir)
    Invoke-TestSuite "termin-base python" (@("-m", "pytest") + $PytestMarkerArgs + @("termin-base/tests/python/") + (New-PytestSuiteArgs "termin-base-python") + @("-v"))
    Invoke-TestSuite "termin-modules import smoke" @("-c", "import termin_modules; env = termin_modules.ModuleEnvironment(); runtime = termin_modules.ModuleRuntime(); runtime.set_environment(env); runtime.register_cpp_backend(termin_modules.CppModuleBackend()); runtime.register_python_backend(termin_modules.PythonModuleBackend())")
    Invoke-TestSuite "termin-mesh python" (@("-m", "pytest") + $PytestMarkerArgs + @("termin-mesh/tests/python/") + (New-PytestSuiteArgs "termin-mesh-python") + @("-v"))
    Invoke-TestSuite "termin-prefab python" (@("-m", "pytest") + $PytestMarkerArgs + @("termin-prefab/tests/") + (New-PytestSuiteArgs "termin-prefab-python") + @("-v"))
    Invoke-TestSuite "termin-glb python" (@("-m", "pytest") + $PytestMarkerArgs + @("termin-glb/tests/") + (New-PytestSuiteArgs "termin-glb-python") + @("-v"))
    Invoke-TestSuite "termin-default-assets python" (@("-m", "pytest") + $PytestMarkerArgs + @("termin-default-assets/tests/") + (New-PytestSuiteArgs "termin-default-assets-python") + @("-v"))
    Invoke-TestSuite "termin-csg python" (@("-m", "pytest") + $PytestMarkerArgs + @("termin-csg/tests/") + (New-PytestSuiteArgs "termin-csg-python") + @("-v"))
    Invoke-TestSuite "termin-graphics python" (@("-m", "pytest") + $PytestMarkerArgs + @("termin-graphics/tests/python/") + (New-PytestSuiteArgs "termin-graphics-python") + @("-v"))
    Invoke-TestSuite "termin-gui python" (@("-m", "pytest") + $PytestMarkerArgs + @("termin-gui/python/tests/") + (New-PytestSuiteArgs "termin-gui-python") + @("-v"))
    Invoke-TestSuite "termin-nodegraph python" (@("-m", "pytest") + $PytestMarkerArgs + @("termin-nodegraph/tests/") + (New-PytestSuiteArgs "termin-nodegraph-python") + @("-v"))
    Invoke-TestSuite "termin-qopt python" (@("-m", "pytest") + $PytestMarkerArgs + @("termin-qopt/tests/") + (New-PytestSuiteArgs "termin-qopt-python") + @("-v"))
    Invoke-TestSuite "termin-pga python" (@("-m", "pytest") + $PytestMarkerArgs + @("termin-pga/tests/") + (New-PytestSuiteArgs "termin-pga-python") + @("-v"))
    Invoke-TestSuite "termin-app python" (@("-m", "pytest") + $PytestMarkerArgs + @("termin-app/tests/") + (New-PytestSuiteArgs "termin-app-python") + @("-v"))
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
