#!/usr/bin/env pwsh
# Build and install C/C++ SDK libraries through the top-level CMake graph.
# This mirrors build-sdk-cpp.sh for Windows/PowerShell.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkPrefix = if ($env:SDK_PREFIX) { $env:SDK_PREFIX } else { Join-Path $ScriptDir "sdk" }
$BuildDirEnv = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { $null }

$BuildType = "Release"
$Clean = $false
$NoParallel = $false
$VulkanMode = "off"
$SdlMode = "on"
$CcacheMode = "on"
$UnityMode = "off"
$PchMode = "off"
$BuildJobs = if ($env:BUILD_JOBS) { [int]$env:BUILD_JOBS } else { [Environment]::ProcessorCount }
$CmakeGeneratorName = if ($env:CMAKE_GENERATOR_NAME) { $env:CMAKE_GENERATOR_NAME } elseif ($env:TERMIN_CMAKE_GENERATOR) { $env:TERMIN_CMAKE_GENERATOR } else { $null }

function Show-Help {
    Write-Host "Usage: .\build-sdk-cpp.ps1 [OPTIONS]"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  --debug, -d       Debug build"
    Write-Host "  --clean, -c       Clean build directory first"
    Write-Host "  --no-parallel     Disable parallel compilation (equivalent to -j1)"
    Write-Host "  --ccache          Use ccache if available (default; ignored by MSVC root graph)"
    Write-Host "  --no-ccache       Disable ccache compiler launcher"
    Write-Host "  --unity           Enable CMake unity build (experimental)"
    Write-Host "  --no-unity        Disable CMake unity build (default)"
    Write-Host "  --pch             Enable precompiled headers for selected C++ targets (experimental)"
    Write-Host "  --no-pch          Disable precompiled headers (default)"
    Write-Host "  --no-vulkan       Disable Vulkan support (default)"
    Write-Host "  --vulkan          Enable Vulkan support"
    Write-Host "  --no-sdl          Disable SDL2 support"
    Write-Host "  --sdl             Enable SDL2 support (default)"
    Write-Host "  --help, -h        Show this help"
    Write-Host ""
    Write-Host "Environment:"
    Write-Host "  SDK_PREFIX        Install prefix (default: .\sdk)"
    Write-Host "  BUILD_DIR         CMake build directory (default: .\build\<BUILD_TYPE>)"
    Write-Host "  BUILD_JOBS        Parallel build jobs (default: logical processor count)"
    Write-Host "  TERMIN_CMAKE_GENERATOR or CMAKE_GENERATOR_NAME"
    Write-Host "                    CMake generator for a new build dir (default: Ninja if available)"
}

foreach ($arg in $args) {
    switch ($arg) {
        "--debug"       { $BuildType = "Debug" }
        "-d"            { $BuildType = "Debug" }
        "--clean"       { $Clean = $true }
        "-c"            { $Clean = $true }
        "--no-parallel" { $NoParallel = $true }
        "--ccache"      { $CcacheMode = "on" }
        "--no-ccache"   { $CcacheMode = "off" }
        "--unity"       { $UnityMode = "on" }
        "--no-unity"    { $UnityMode = "off" }
        "--pch"         { $PchMode = "on" }
        "--no-pch"      { $PchMode = "off" }
        "--no-vulkan"   { $VulkanMode = "off" }
        "--vulkan"      { $VulkanMode = "on" }
        "--no-sdl"      { $SdlMode = "off" }
        "--sdl"         { $SdlMode = "on" }
        "--help"        { Show-Help; exit 0 }
        "-h"            { Show-Help; exit 0 }
        default          { Write-Error "Unknown option: $arg"; exit 1 }
    }
}

if ($NoParallel) {
    $BuildJobs = 1
}

$BuildDir = if ($BuildDirEnv) { $BuildDirEnv } else { Join-Path (Join-Path $ScriptDir "build") $BuildType }

if (-not $CmakeGeneratorName -and -not (Test-Path (Join-Path $BuildDir "CMakeCache.txt")) -and (Get-Command ninja -ErrorAction SilentlyContinue)) {
    $CmakeGeneratorName = "Ninja"
}

$TerminEnableVulkan = if ($VulkanMode -eq "on") { "ON" } else { "OFF" }
$TerminEnableSdl = if ($SdlMode -eq "on") { "ON" } else { "OFF" }
$TerminUseCcache = if ($CcacheMode -eq "on") { "ON" } else { "OFF" }
$TerminEnableUnityBuild = if ($UnityMode -eq "on") { "ON" } else { "OFF" }
$TerminEnablePch = if ($PchMode -eq "on") { "ON" } else { "OFF" }

Write-Host ""
Write-Host "========================================"
Write-Host "  Building Termin C/C++ SDK ($BuildType)"
Write-Host "  mode: top-level CMake graph"
Write-Host "========================================"
Write-Host ""
Write-Host "Source dir:  $ScriptDir"
Write-Host "Build dir:   $BuildDir"
Write-Host "SDK prefix:  $SdkPrefix"
Write-Host "Vulkan:      $TerminEnableVulkan"
Write-Host "SDL2:        $TerminEnableSdl"
Write-Host "ccache:      $TerminUseCcache"
Write-Host "Unity build: $TerminEnableUnityBuild"
Write-Host "PCH:         $TerminEnablePch"
Write-Host "Generator:   $(if ($CmakeGeneratorName) { $CmakeGeneratorName } else { 'existing/default' })"
Write-Host "Jobs:        $BuildJobs"
Write-Host ""

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning $BuildDir..."
    Remove-Item -Recurse -Force $BuildDir
}

$cmakeArgs = @()
if ($CmakeGeneratorName -and -not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    $cmakeArgs += @("-G", $CmakeGeneratorName)
}

$cmakeArgs += @(
    "-S", $ScriptDir,
    "-B", $BuildDir,
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_INSTALL_PREFIX=$SdkPrefix",
    "-DCMAKE_PREFIX_PATH=$SdkPrefix",
    "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
    "-DTERMIN_USE_CCACHE=$TerminUseCcache",
    "-DTERMIN_ENABLE_UNITY_BUILD=$TerminEnableUnityBuild",
    "-DTERMIN_ENABLE_PCH=$TerminEnablePch",
    "-DTERMIN_BUILD_PYTHON=OFF",
    "-DTERMIN_BUILD_TESTS=OFF",
    "-DTERMIN_ENABLE_VULKAN=$TerminEnableVulkan",
    "-DTERMIN_ENABLE_SDL=$TerminEnableSdl",
    "-DTERMIN_BUILD_EDITOR_MINIMAL=OFF",
    "-DTERMIN_BUILD_EDITOR_EXE=OFF",
    "-DTERMIN_BUILD_LAUNCHER=OFF"
)

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

& cmake --build $BuildDir --config $BuildType --parallel $BuildJobs
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

& cmake --install $BuildDir --config $BuildType
if ($LASTEXITCODE -ne 0) { throw "cmake install failed" }

Write-Host ""
Write-Host "========================================"
Write-Host "  C/C++ SDK installed to $SdkPrefix"
Write-Host "========================================"
