#!/usr/bin/env pwsh
# Build the SDK using the dedicated stage scripts:
#   1. build-sdk-cpp.ps1    — C/C++ libraries
#   2. build-sdk-bindings.ps1 — Python bindings (nanobind)
#   3. build-sdk-csharp.ps1  — C# bindings
#
# To install pip packages into your Python environment, run separately:
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
        Write-Host "  --help, -h        Show this help"
        exit 0
    }
}

Write-Host ""
Write-Host "========================================"
Write-Host "  Stage 1/3: C/C++ libraries"
Write-Host "========================================"
Write-Host ""
& (Join-Path $ScriptDir "build-sdk-cpp.ps1") @args
if ($LASTEXITCODE -ne 0) { throw "C++ build failed" }

Write-Host ""
Write-Host "========================================"
Write-Host "  Stage 2/3: Python bindings (nanobind)"
Write-Host "========================================"
Write-Host ""
& (Join-Path $ScriptDir "build-sdk-bindings.ps1") @args
if ($LASTEXITCODE -ne 0) { throw "Bindings build failed" }

Write-Host ""
Write-Host "========================================"
Write-Host "  Stage 3/3: C# bindings"
Write-Host "========================================"
Write-Host ""
& (Join-Path $ScriptDir "build-sdk-csharp.ps1") @args
if ($LASTEXITCODE -ne 0) { throw "C# build failed" }

Write-Host ""
Write-Host "========================================"
Write-Host "  All done!"
Write-Host "========================================"
