#!/usr/bin/env pwsh
# Build Python bindings (nanobind SDK + C++ libs with Python) into SDK.
# Assumes C++ libraries are already built via build-sdk-cpp.ps1.
# Reads module list from modules.conf.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkPrefix = if ($env:SDK_PREFIX) { $env:SDK_PREFIX } else { Join-Path $ScriptDir "sdk" }

$BuildType = "Release"
$Clean = $false
$UseParallel = $false
$VulkanMode = "auto"
$SdlMode = "auto"

foreach ($arg in $args) {
    switch ($arg) {
        "--debug"  { $BuildType = "Debug" }
        "-d"       { $BuildType = "Debug" }
        "--clean"  { $Clean = $true }
        "-c"       { $Clean = $true }
        "--no-parallel" { $UseParallel = $false }
        "--no-vulkan"   { $VulkanMode = "off" }
        "--vulkan"      { $VulkanMode = "on" }
        "--no-sdl"      { $SdlMode = "off" }
        "--sdl"         { $SdlMode = "on"  }
        "--help"   { Write-Host "Usage: .\build-sdk-bindings.ps1 [--debug] [--clean] [--no-parallel] [--no-vulkan|--vulkan] [--no-sdl|--sdl]"; exit 0 }
        "-h"       { Write-Host "Usage: .\build-sdk-bindings.ps1 [--debug] [--clean] [--no-parallel] [--no-vulkan|--vulkan] [--no-sdl|--sdl]"; exit 0 }
        default    { Write-Error "Unknown option: $arg"; exit 1 }
    }
}

$CommonFeatureArgs = @()
switch ($VulkanMode) {
    "off" { $CommonFeatureArgs += "-DTGFX2_ENABLE_VULKAN=OFF" }
    "on"  { $CommonFeatureArgs += "-DTGFX2_ENABLE_VULKAN=ON" }
}
switch ($SdlMode) {
    "off" { $CommonFeatureArgs += "-DUSE_SYSTEM_SDL2=OFF" }
    "on"  { $CommonFeatureArgs += "-DUSE_SYSTEM_SDL2=ON" }
}

$pythonExec = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $pythonExec) {
    $pythonExec = (Get-Command python3 -ErrorAction SilentlyContinue).Source
}
if (-not $pythonExec) {
    throw "Python executable not found in PATH"
}

# ── 1. nanobind SDK ──
Write-Host ""
Write-Host "========================================"
Write-Host "  Building termin-nanobind-sdk ($BuildType)"
Write-Host "========================================"
Write-Host ""

& $pythonExec -c "import nanobind" 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "nanobind not installed. Run: pip install nanobind"
}

Push-Location (Join-Path $ScriptDir "termin-nanobind-sdk")
try {
    $buildDir = Join-Path "build" $BuildType
    if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }
    if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }

    $cmakeArgs = @(
        "-S", ".",
        "-B", $buildDir,
        "-DCMAKE_BUILD_TYPE=$BuildType",
        "-DCMAKE_INSTALL_PREFIX=$SdkPrefix",
        "-DPython_EXECUTABLE=$pythonExec"
    )
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
    $buildArgs = @("--build", $buildDir, "--config", $BuildType)
    if ($UseParallel) {
        $buildArgs += "--parallel"
    }
    & cmake @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
    & cmake --install $buildDir --config $BuildType
    if ($LASTEXITCODE -ne 0) { throw "cmake install failed" }
}
finally { Pop-Location }

# ── 2. Python bindings from modules.conf ──
function Build-WithPython {
    param(
        [string]$Name,
        [string]$Dir
    )

    Write-Host ""
    Write-Host "========================================"
    Write-Host "  Building $Name Python bindings ($BuildType)"
    Write-Host "========================================"
    Write-Host ""

    Push-Location $Dir
    try {
        $buildDir = Join-Path "build" $BuildType
        if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }
        if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }

        $cmakeArgs = @(
            "-S", ".",
            "-B", $buildDir,
            "-DCMAKE_BUILD_TYPE=$BuildType",
            "-DCMAKE_INSTALL_PREFIX=$SdkPrefix",
            "-DCMAKE_PREFIX_PATH=$SdkPrefix",
            "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
            "-DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON",
            "-DTERMIN_BUILD_PYTHON=ON",
            "-DPython_EXECUTABLE=$pythonExec"
        ) + $CommonFeatureArgs
        & cmake @cmakeArgs
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
        $buildArgs = @("--build", $buildDir, "--config", $BuildType)
        if ($UseParallel) {
            $buildArgs += "--parallel"
        }
        & cmake @buildArgs
        if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
        & cmake --install $buildDir --config $BuildType
        if ($LASTEXITCODE -ne 0) { throw "cmake install failed" }

        Write-Host "$Name Python bindings installed to $SdkPrefix"
    }
    finally { Pop-Location }
}

# Pre-termin modules
$modulesConf = Join-Path $ScriptDir "modules.conf"
$beforeTermin = $true
foreach ($line in Get-Content $modulesConf) {
    $line = ($line -replace '#.*$', '').Trim()
    if (-not $line) { continue }

    if ($line -eq "@termin-cpp") {
        $beforeTermin = $false
        continue
    }
    if ($line.StartsWith("@")) { continue }
    if (-not $beforeTermin) { continue }

    $parts = $line -split '\|'
    $name = $parts[0].Trim()
    $dir = $parts[1].Trim()
    $hasPython = $parts[2].Trim()

    if ($hasPython -ne "yes") { continue }

    Build-WithPython -Name $name -Dir (Join-Path $ScriptDir $dir)
}

# ── 3. termin bundle ──
Write-Host ""
Write-Host "========================================"
Write-Host "  Building termin ($BuildType)"
Write-Host "========================================"
Write-Host ""

Push-Location (Join-Path $ScriptDir "termin-app")
try {
    $buildArgs = @()
    if ($BuildType -eq "Debug") { $buildArgs += "-Debug" }
    if ($Clean) { $buildArgs += "-Clean" }

    $oldPrefix = $env:CMAKE_PREFIX_PATH
    try {
        $env:CMAKE_PREFIX_PATH = if ($oldPrefix) { "$SdkPrefix;$oldPrefix" } else { $SdkPrefix }
        $env:SDK_PREFIX = $SdkPrefix
        if (-not $UseParallel) {
            $buildArgs += "-NoParallel"
        }
        & .\build.ps1 @buildArgs
        if ($LASTEXITCODE -ne 0) { throw "termin build failed" }
    }
    finally {
        if ($null -eq $oldPrefix) {
            Remove-Item Env:CMAKE_PREFIX_PATH -ErrorAction SilentlyContinue
        } else {
            $env:CMAKE_PREFIX_PATH = $oldPrefix
        }
    }
}
finally { Pop-Location }

$terminInstall = Join-Path (Join-Path $ScriptDir "termin-app") "install_win"
if (Test-Path $terminInstall) {
    Write-Host "Installing termin to $SdkPrefix..."
    Copy-Item -Recurse -Force "$terminInstall\*" $SdkPrefix
}

# Post-termin modules
$afterTermin = $false
foreach ($line in Get-Content $modulesConf) {
    $line = ($line -replace '#.*$', '').Trim()
    if (-not $line) { continue }

    if ($line -eq "@termin-cpp") {
        $afterTermin = $true
        continue
    }
    if ($line.StartsWith("@")) { continue }
    if (-not $afterTermin) { continue }

    $parts = $line -split '\|'
    $name = $parts[0].Trim()
    $dir = $parts[1].Trim()
    $hasPython = $parts[2].Trim()

    if ($hasPython -ne "yes") { continue }

    Build-WithPython -Name $name -Dir (Join-Path $ScriptDir $dir)
}

Write-Host ""
Write-Host "========================================"
Write-Host "  All bindings done!"
Write-Host "========================================"
