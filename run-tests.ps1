#!/usr/bin/env pwsh
# Run all repo tests: C/C++ first, then Python.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

foreach ($arg in $args) {
    if ($arg -eq "--help" -or $arg -eq "-h") {
        Write-Host "Usage: .\run-tests.ps1 [run-tests-cpp options]"
        Write-Host ""
        Write-Host "Vulkan is enabled by default for C/C++ tests."
        Write-Host "Use --no-vulkan only for OpenGL/legacy compatibility checks."
        exit 0
    }
}

$Failures = New-Object System.Collections.Generic.List[string]

try {
    & (Join-Path $ScriptDir "run-tests-cpp.ps1") @args
    if ($LASTEXITCODE -ne 0) {
        $Failures.Add("C/C++")
    }
} catch {
    Write-Warning "[run-tests] C/C++ test runner failed: $_"
    $Failures.Add("C/C++")
}

try {
    & (Join-Path $ScriptDir "run-tests-python.ps1")
    if ($LASTEXITCODE -ne 0) {
        $Failures.Add("Python")
    }
} catch {
    Write-Warning "[run-tests] Python test runner failed: $_"
    $Failures.Add("Python")
}

if ($Failures.Count -gt 0) {
    Write-Host ""
    Write-Host "========================================"
    Write-Host "  Test failures"
    Write-Host "========================================"
    foreach ($failure in $Failures) {
        Write-Host "  - $failure"
    }
    exit 1
}

Write-Host ""
Write-Host "========================================"
Write-Host "  All tests passed"
Write-Host "========================================"
