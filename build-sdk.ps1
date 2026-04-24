#!/usr/bin/env pwsh
# Build the SDK using the dedicated stage scripts:
#   1. build-sdk-cpp.ps1        — C/C++ libraries
#   2. build-sdk-bindings.ps1   — Python bindings (nanobind)
#   3. build-sdk-csharp.ps1     — C# bindings
#   4. install-pip-packages.ps1 — populate bundled Python's site-packages
#
# To install pip packages into your own user Python environment, run separately:
#   .\install-pip-packages.ps1

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

foreach ($arg in $args) {
    if ($arg -eq "--help" -or $arg -eq "-h") {
        Write-Host "Usage: .\build-sdk.ps1 [OPTIONS]"
        Write-Host ""
        Write-Host "Options:"
        Write-Host "  --debug, -d       Debug build"
        Write-Host "  --clean, -c       Clean build directories first"
        Write-Host "  --no-parallel     Disable parallel compilation"
        Write-Host "  --no-vulkan       Disable Vulkan support"
        Write-Host "  --vulkan          Force Vulkan support on"
        Write-Host "  --no-sdl          Disable SDL2 support"
        Write-Host "  --sdl             Force SDL2 support on"
        Write-Host "  --help, -h        Show this help"
        exit 0
    }
}

Write-Host ""
Write-Host "========================================"
Write-Host "  Stage 1/4: C/C++ libraries"
Write-Host "========================================"
Write-Host ""
& (Join-Path $ScriptDir "build-sdk-cpp.ps1") @args
if ($LASTEXITCODE -ne 0) { throw "C++ build failed" }

Write-Host ""
Write-Host "========================================"
Write-Host "  Stage 2/4: Python bindings (nanobind)"
Write-Host "========================================"
Write-Host ""
& (Join-Path $ScriptDir "build-sdk-bindings.ps1") @args
if ($LASTEXITCODE -ne 0) { throw "Bindings build failed" }

Write-Host ""
Write-Host "========================================"
Write-Host "  Stage 3/4: C# bindings"
Write-Host "========================================"
Write-Host ""
& (Join-Path $ScriptDir "build-sdk-csharp.ps1") @args
if ($LASTEXITCODE -ne 0) { throw "C# build failed" }

Write-Host ""
Write-Host "========================================"
Write-Host "  Stage 4/4: Populate bundled Python site-packages"
Write-Host "========================================"
Write-Host ""

# Resolve the Python version used by the bundled interpreter. Stage 1
# installs the stdlib under sdk/lib/python<MAJOR>.<MINOR>/ (only when
# BUNDLE_PYTHON=ON during the termin CMake build), so we probe for that
# directory and target its site-packages.
$SdkPrefix = if ($env:SDK_PREFIX) { $env:SDK_PREFIX } else { Join-Path $ScriptDir "sdk" }
$BundledPyDir = Get-ChildItem -Path (Join-Path $SdkPrefix "lib") -Directory -Filter "python3.*" -ErrorAction SilentlyContinue | Select-Object -First 1

if (-not $BundledPyDir) {
    Write-Host "WARNING: bundled Python stdlib not found under $SdkPrefix\lib\python3.*" -ForegroundColor Yellow
    Write-Host "  Skipping pip install into bundled site-packages."
    Write-Host "  Was BUNDLE_PYTHON=ON during the termin CMake build?"
} else {
    $BundledSitePackages = Join-Path $BundledPyDir.FullName "site-packages"
    Write-Host "Bundled Python stdlib:        $($BundledPyDir.FullName)"
    Write-Host "Bundled Python site-packages: $BundledSitePackages"
    Write-Host ""

    $env:TERMIN_SDK = $SdkPrefix
    & (Join-Path $ScriptDir "install-pip-packages.ps1") --target $BundledSitePackages
    if ($LASTEXITCODE -ne 0) { throw "pip install into bundled site-packages failed" }
}

Write-Host ""
Write-Host "========================================"
Write-Host "  All done!"
Write-Host "========================================"
