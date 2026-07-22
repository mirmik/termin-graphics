#!/usr/bin/env pwsh
# Run repo tests: working set by default, full set on request.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Full = $false
$ProcessSmokeOnly = $false
$ProcessSmokeDisabled = $false
$ProcessSmokeProfile = if ($env:TERMIN_PROCESS_SMOKE_PROFILE) { $env:TERMIN_PROCESS_SMOKE_PROFILE } else { "" }
$CppArgs = New-Object System.Collections.Generic.List[string]

foreach ($arg in $args) {
    switch ($arg) {
        "--full" {
            $Full = $true
            $CppArgs.Add("--full")
        }
        "--process-smoke-only" {
            $ProcessSmokeOnly = $true
        }
        "--no-process-smoke" {
            $ProcessSmokeDisabled = $true
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
            Write-Host "  --process-smoke-only"
            Write-Host "              Run only the selected manifest process-smoke profile"
            Write-Host "  --process-smoke-profile=<profile>"
            Write-Host "              Select a process-smoke profile (full defaults to windows-d3d11-bound)"
            Write-Host "  --no-process-smoke"
            Write-Host "              Disable process-smoke execution"
            Write-Host "  --help, -h  Show this help"
            exit 0
        }
        "-h" {
            Write-Host "Usage: .\run-tests.ps1 [OPTIONS] [run-tests-cpp options]"
            Write-Host "Use --full to include window/full C++ tests and pytest tests marked full."
            exit 0
        }
        default {
            if ($arg.StartsWith("--process-smoke-profile=")) {
                $ProcessSmokeProfile = $arg.Substring("--process-smoke-profile=".Length)
            } else {
                $CppArgs.Add($arg)
            }
        }
    }
}

$Failures = New-Object System.Collections.Generic.List[string]

if ($ProcessSmokeOnly -and $ProcessSmokeDisabled) {
    throw "--process-smoke-only cannot be combined with --no-process-smoke"
}

if (-not $ProcessSmokeOnly) {
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
}

if (-not $ProcessSmokeDisabled) {
    if (-not $ProcessSmokeProfile -and ($Full -or $ProcessSmokeOnly)) {
        $ProcessSmokeProfile = "windows-d3d11-bound"
    }
    if ($ProcessSmokeProfile) {
        try {
            $PythonCommand = Get-Command python -ErrorAction Stop
            $PythonExe = $PythonCommand.Path
            $env:PYTHONPATH = (Join-Path $ScriptDir "termin-build-tools") + $(
                if ($env:PYTHONPATH) { [IO.Path]::PathSeparator + $env:PYTHONPATH } else { "" }
            )
            $BuildType = "Release"
            if ($CppArgs.Contains("--debug") -or $CppArgs.Contains("-d")) {
                $BuildType = "Debug"
            }
            $ProcessRoot = Join-Path (Join-Path $ScriptDir "build\process-smoke") $ProcessSmokeProfile
            New-Item -ItemType Directory -Force -Path $ProcessRoot | Out-Null
            $PlanPath = Join-Path $ProcessRoot "expected.json"
            $ReportPath = Join-Path $ProcessRoot "execution-manifest.json"
            $LogDir = Join-Path $ProcessRoot "logs"
            $RepositoryControl = @(
                "-m", "termin_build.repository_control",
                "--repo-root", $ScriptDir
            )
            $ExpectedJson = & $PythonExe @RepositoryControl plan $ProcessSmokeProfile `
                --platform windows --json
            if ($LASTEXITCODE -ne 0) {
                throw "Process-smoke expected plan generation failed"
            }
            $Utf8NoBom = [Text.UTF8Encoding]::new($false)
            [IO.File]::WriteAllText(
                $PlanPath,
                ($ExpectedJson -join [Environment]::NewLine) + [Environment]::NewLine,
                $Utf8NoBom
            )
            $Capabilities = @()
            if ($env:TERMIN_PROCESS_SMOKE_CAPABILITIES) {
                $Capabilities = $env:TERMIN_PROCESS_SMOKE_CAPABILITIES -split "[,;\s]+" |
                    Where-Object { $_ }
            } elseif ($ProcessSmokeProfile -eq "windows-d3d11-bound") {
                $Capabilities = @("d3d11")
            }
            $CapabilityArgs = @()
            foreach ($capability in $Capabilities) {
                $CapabilityArgs += @("--capability", $capability)
            }
            $Timeout = if ($env:TERMIN_PROCESS_SMOKE_TIMEOUT) {
                [double]$env:TERMIN_PROCESS_SMOKE_TIMEOUT
            } else {
                900.0
            }
            & $PythonExe @RepositoryControl run $ProcessSmokeProfile `
                --platform windows `
                --executor process-smoke `
                --configuration $BuildType `
                --process-timeout $Timeout `
                --process-log-dir $LogDir `
                --report-output $ReportPath `
                @CapabilityArgs
            $ProcessExit = $LASTEXITCODE
            & $PythonExe @RepositoryControl verify-suite-execution `
                --plan $PlanPath `
                --manifest $ReportPath `
                --executor process-smoke
            $VerifyExit = $LASTEXITCODE
            if ($ProcessExit -ne 0 -or $VerifyExit -ne 0) {
                $Failures.Add("Process smoke")
            }
        } catch {
            Write-Warning "[run-tests] Process-smoke runner failed: $_"
            $Failures.Add("Process smoke")
        }
    } else {
        Write-Host "Process-smoke tests skipped (use --full or --process-smoke-profile=<profile>)"
    }
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
