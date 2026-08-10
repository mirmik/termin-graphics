#!/usr/bin/env pwsh
# Install the pinned Slang compiler and register it in common Termin settings.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$TerminPython = if ($env:TERMIN_PYTHON) {
    $env:TERMIN_PYTHON
} else {
    Join-Path $ScriptDir "sdk\bin\termin_python.exe"
}

if (-not (Test-Path $TerminPython -PathType Leaf)) {
    throw "Termin SDK Python is missing: $TerminPython. Build the SDK first with .\build-sdk.ps1, or set TERMIN_PYTHON."
}

& $TerminPython (Join-Path $ScriptDir "scripts\install_slang_toolchain.py") @args
exit $LASTEXITCODE
