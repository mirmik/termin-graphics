#!/usr/bin/env pwsh
# Build and install C/C++ libraries into SDK (no Python bindings).
# Reads module list from modules.conf.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkPrefix = if ($env:SDK_PREFIX) { $env:SDK_PREFIX } else { Join-Path $ScriptDir "sdk" }

$BuildType = "Release"
$Clean = $false

foreach ($arg in $args) {
    switch ($arg) {
        "--debug"  { $BuildType = "Debug" }
        "-d"       { $BuildType = "Debug" }
        "--clean"  { $Clean = $true }
        "-c"       { $Clean = $true }
        "--help"   { Write-Host "Usage: .\build-sdk-cpp.ps1 [--debug] [--clean]"; exit 0 }
        "-h"       { Write-Host "Usage: .\build-sdk-cpp.ps1 [--debug] [--clean]"; exit 0 }
        default    { Write-Error "Unknown option: $arg"; exit 1 }
    }
}

function Build-CppLib {
    param(
        [string]$Name,
        [string]$Dir,
        [string[]]$ExtraCmakeArgs = @()
    )

    Write-Host ""
    Write-Host "========================================"
    Write-Host "  Building $Name ($BuildType) [C/C++ only]"
    Write-Host "========================================"
    Write-Host ""

    Push-Location $Dir
    try {
        $buildDir = Join-Path "build" $BuildType

        if ($Clean -and (Test-Path $buildDir)) {
            Write-Host "Cleaning $buildDir..."
            Remove-Item -Recurse -Force $buildDir
        }

        if (-not (Test-Path $buildDir)) {
            New-Item -ItemType Directory -Path $buildDir | Out-Null
        }

        $cmakeArgs = @(
            "-S", ".",
            "-B", $buildDir,
            "-DCMAKE_BUILD_TYPE=$BuildType",
            "-DCMAKE_INSTALL_PREFIX=$SdkPrefix",
            "-DCMAKE_PREFIX_PATH=$SdkPrefix",
            "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
            "-DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON",
            "-DTERMIN_BUILD_PYTHON=OFF"
        ) + $ExtraCmakeArgs

        & cmake @cmakeArgs
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
        & cmake --build $buildDir --config $BuildType --parallel
        if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
        & cmake --install $buildDir --config $BuildType
        if ($LASTEXITCODE -ne 0) { throw "cmake install failed" }

        Write-Host "$Name installed to $SdkPrefix"
    }
    finally {
        Pop-Location
    }
}

function Build-TerminCppOnly {
    Write-Host ""
    Write-Host "========================================"
    Write-Host "  Building termin ($BuildType) [C/C++ only]"
    Write-Host "========================================"
    Write-Host ""

    Push-Location (Join-Path $ScriptDir "termin")
    try {
        $buildDir = Join-Path "build_standalone_cpp" $BuildType

        if ($Clean -and (Test-Path $buildDir)) {
            Remove-Item -Recurse -Force $buildDir
        }

        if (-not (Test-Path $buildDir)) {
            New-Item -ItemType Directory -Path $buildDir | Out-Null
        }

        $cmakeArgs = @(
            "-S", ".",
            "-B", $buildDir,
            "-DCMAKE_BUILD_TYPE=$BuildType",
            "-DCMAKE_INSTALL_PREFIX=$SdkPrefix",
            "-DCMAKE_PREFIX_PATH=$SdkPrefix",
            "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
            "-DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON",
            "-DBUILD_EDITOR_MINIMAL=OFF",
            "-DBUILD_EDITOR_EXE=OFF",
            "-DBUILD_LAUNCHER=OFF",
            "-DBUNDLE_PYTHON=OFF",
            "-DTERMIN_BUILD_PYTHON=OFF"
        )

        & cmake @cmakeArgs
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
        & cmake --build $buildDir --config $BuildType --parallel
        if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
        & cmake --install $buildDir --config $BuildType
        if ($LASTEXITCODE -ne 0) { throw "cmake install failed" }

        Write-Host "termin (C/C++ only) installed to $SdkPrefix"
    }
    finally {
        Pop-Location
    }
}

# Read modules.conf and build
$modulesConf = Join-Path $ScriptDir "modules.conf"
foreach ($line in Get-Content $modulesConf) {
    $line = ($line -replace '#.*$', '').Trim()
    if (-not $line) { continue }

    if ($line -eq "@termin-cpp") {
        Build-TerminCppOnly
        continue
    }
    if ($line.StartsWith("@")) { continue }

    $parts = $line -split '\|'
    $name = $parts[0].Trim()
    $dir = $parts[1].Trim()
    $extraCmake = if ($parts.Count -ge 5) { $parts[4].Trim() } else { "-" }

    $extraArgs = @()
    if ($extraCmake -ne "-" -and $extraCmake) {
        $extraArgs = $extraCmake -split '\s+'
    }

    Build-CppLib -Name $name -Dir (Join-Path $ScriptDir $dir) -ExtraCmakeArgs $extraArgs
}

Write-Host ""
Write-Host "========================================"
Write-Host "  All done (C/C++ only)!"
Write-Host "========================================"
