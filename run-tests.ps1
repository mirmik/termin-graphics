#!/usr/bin/env pwsh
# Run repo tests: working set by default, full set on request.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Full = $false
$CppArgs = New-Object System.Collections.Generic.List[string]

foreach ($arg in $args) {
    switch ($arg) {
        "--full" {
            $Full = $true
            $CppArgs.Add("--full")
        }
        "--help" {
            Write-Host "Usage: .\run-tests.ps1 [OPTIONS] [run-tests-cpp options]"
            Write-Host ""
            Write-Host "By default this runs the working test set: no window tests"
            Write-Host "and no pytest tests marked full."
            Write-Host ""
            Write-Host "Vulkan is enabled by default for C/C++ tests."
            Write-Host "Use --no-vulkan only for OpenGL/legacy compatibility checks."
            Write-Host ""
            Write-Host "Options:"
            Write-Host "  --full      Include window/full C++ tests and pytest tests marked full"
            Write-Host "  --help, -h  Show this help"
            exit 0
        }
        "-h" {
            Write-Host "Usage: .\run-tests.ps1 [OPTIONS] [run-tests-cpp options]"
            Write-Host "Use --full to include window/full C++ tests and pytest tests marked full."
            exit 0
        }
        default {
            $CppArgs.Add($arg)
        }
    }
}

$Failures = New-Object System.Collections.Generic.List[string]

try {
    $CppArgArray = $CppArgs.ToArray()
    & (Join-Path $ScriptDir "run-tests-cpp.ps1") @CppArgArray
    if ($LASTEXITCODE -ne 0) {
        $Failures.Add("C/C++")
    }
} catch {
    Write-Warning "[run-tests] C/C++ test runner failed: $_"
    $Failures.Add("C/C++")
}

try {
    $PythonArgs = @()
    if ($Full) {
        $PythonArgs += "--full"
    }
    & (Join-Path $ScriptDir "run-tests-python.ps1") @PythonArgs
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
