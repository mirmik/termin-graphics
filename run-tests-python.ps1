#!/usr/bin/env pwsh
# Run Python test suites across projects.
#
# By default, auto-activates .venv/ if present and auto-detects TERMIN_SDK,
# so no manual setup is needed after setting up the editable test environment.
#
# Usage:
#   .\run-tests-python.ps1
#   .\run-tests-python.ps1 --no-venv
#   .\run-tests-python.ps1 termin-app/tests/test_project_file_watcher.py -q

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$NoVenv = $false
$PytestTargets = New-Object System.Collections.Generic.List[string]

function Show-Help {
    Write-Host "Usage: .\run-tests-python.ps1 [--no-venv] [pytest-target ...]"
    Write-Host ""
    Write-Host "  (no flags)  Auto-activate .venv/ if present, auto-detect TERMIN_SDK"
    Write-Host "  --no-venv   Skip auto-activation; use PYTHON_BIN or system Python"
    Write-Host "  pytest-target"
    Write-Host "              Run only selected pytest target(s), e.g. termin-app/tests/test_game_mode_model.py"
}

foreach ($arg in $args) {
    if ($arg -eq "--no-venv") {
        $NoVenv = $true
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

if (-not $NoVenv) {
    $venvActivateCandidates = @(
        (Join-Path $ScriptDir ".venv\Scripts\Activate.ps1"),
        (Join-Path $ScriptDir ".venv\bin\Activate.ps1")
    )
    foreach ($activatePath in $venvActivateCandidates) {
        if (Test-Path $activatePath) {
            Write-Host "Activating venv: $(Join-Path $ScriptDir '.venv')"
            . $activatePath
            break
        }
    }
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

$pythonPathEntries = New-Object System.Collections.Generic.List[string]
if ($NoVenv -or -not (Test-Path (Join-Path $ScriptDir ".venv"))) {
    $bundledSitePackages = $null
    $windowsSitePackages = Join-Path $SdkPrefix "Lib\site-packages"
    if (Test-Path $windowsSitePackages) {
        $bundledSitePackages = $windowsSitePackages
    } else {
        $libDir = Join-Path $SdkPrefix "lib"
        $pyDir = Get-ChildItem -Path $libDir -Directory -Filter "python3.*" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($pyDir) {
            $candidate = Join-Path $pyDir.FullName "site-packages"
            if (Test-Path $candidate) {
                $bundledSitePackages = $candidate
            }
        }
    }

    if ($bundledSitePackages) {
        $pythonPathEntries.Add($bundledSitePackages)
    }
    $pythonPathEntries.Add((Join-Path $ScriptDir "termin-app\install\lib\python"))
}
if ($env:PYTHONPATH) {
    $pythonPathEntries.Add($env:PYTHONPATH)
}
$env:PYTHONPATH = ($pythonPathEntries -join [IO.Path]::PathSeparator)

Write-Host ""
Write-Host "========================================"
Write-Host "  Python tests"
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
    Invoke-TestSuite "selected python" (@("-m", "pytest") + $PytestTargets.ToArray() + (New-PytestSuiteArgs "selected-python") + @("-v"))
} else {
    Invoke-TestSuite "termin-base python" (@("-m", "pytest", "termin-base/tests/python/") + (New-PytestSuiteArgs "termin-base-python") + @("-v"))
    Invoke-TestSuite "termin-modules import smoke" @("-c", "import termin_modules; env = termin_modules.ModuleEnvironment(); runtime = termin_modules.ModuleRuntime(); runtime.set_environment(env); runtime.register_cpp_backend(termin_modules.CppModuleBackend()); runtime.register_python_backend(termin_modules.PythonModuleBackend())")
    Invoke-TestSuite "termin-mesh python" (@("-m", "pytest", "termin-mesh/tests/python/") + (New-PytestSuiteArgs "termin-mesh-python") + @("-v"))
    Invoke-TestSuite "termin-prefab python" (@("-m", "pytest", "termin-prefab/tests/") + (New-PytestSuiteArgs "termin-prefab-python") + @("-v"))
    Invoke-TestSuite "termin-glb python" (@("-m", "pytest", "termin-glb/tests/") + (New-PytestSuiteArgs "termin-glb-python") + @("-v"))
    Invoke-TestSuite "termin-default-assets python" (@("-m", "pytest", "termin-default-assets/tests/") + (New-PytestSuiteArgs "termin-default-assets-python") + @("-v"))
    Invoke-TestSuite "termin-csg python" (@("-m", "pytest", "termin-csg/tests/") + (New-PytestSuiteArgs "termin-csg-python") + @("-v"))
    Invoke-TestSuite "termin-graphics python" (@("-m", "pytest", "termin-graphics/tests/python/") + (New-PytestSuiteArgs "termin-graphics-python") + @("-v"))
    Invoke-TestSuite "termin-gui python" (@("-m", "pytest", "termin-gui/python/tests/") + (New-PytestSuiteArgs "termin-gui-python") + @("-v"))
    Invoke-TestSuite "termin-nodegraph python" (@("-m", "pytest", "termin-nodegraph/tests/") + (New-PytestSuiteArgs "termin-nodegraph-python") + @("-v"))
    Invoke-TestSuite "termin-qopt python" (@("-m", "pytest", "termin-qopt/tests/") + (New-PytestSuiteArgs "termin-qopt-python") + @("-v"))
    Invoke-TestSuite "termin-pga python" (@("-m", "pytest", "termin-pga/tests/") + (New-PytestSuiteArgs "termin-pga-python") + @("-v"))
    Invoke-TestSuite "termin-app python" (@("-m", "pytest", "termin-app/tests/") + (New-PytestSuiteArgs "termin-app-python") + @("-v"))
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
