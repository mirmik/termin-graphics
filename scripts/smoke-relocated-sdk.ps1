$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$SdkRoot = if ($env:SDK_PREFIX) { $env:SDK_PREFIX } else { Join-Path $RepoRoot "sdk" }
$PythonCommand = if ($env:PYTHON_BIN) { $env:PYTHON_BIN } else { "python" }
$PreviousPythonPath = $env:PYTHONPATH

try {
    $env:PYTHONPATH = Join-Path $RepoRoot "termin-build-tools"
    & $PythonCommand -m termin_build.relocated_sdk_smoke --sdk-root $SdkRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Relocated SDK smoke failed with exit code $LASTEXITCODE"
    }
}
finally {
    $env:PYTHONPATH = $PreviousPythonPath
}
