#!/usr/bin/env pwsh
# Run C/C++ test suites through the top-level CMake graph.
# Assumes SDK dependencies are available, typically via:
#   .\build-sdk-cpp.ps1

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkPrefix = if ($env:SDK_PREFIX) { $env:SDK_PREFIX } else { Join-Path $ScriptDir "sdk" }
$BuildType = "Release"
$BuildJobs = if ($env:BUILD_JOBS) { [int]$env:BUILD_JOBS } else { [Environment]::ProcessorCount }
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "" }
$VulkanMode = "on"
$SdlMode = "on"
$WindowTestsMode = "auto"
$CcacheMode = "on"
$UnityMode = "off"
$PchMode = "on"
$CmakeGeneratorName = if ($env:CMAKE_GENERATOR_NAME) { $env:CMAKE_GENERATOR_NAME } elseif ($env:TERMIN_CMAKE_GENERATOR) { $env:TERMIN_CMAKE_GENERATOR } else { $null }

function Get-CMakeGeneratorFromCache {
    param([string]$BuildDir)

    $cachePath = Join-Path $BuildDir "CMakeCache.txt"
    if (-not (Test-Path $cachePath)) {
        return ""
    }

    $generatorLine = Get-Content $cachePath | Where-Object { $_ -like "CMAKE_GENERATOR:INTERNAL=*" } | Select-Object -First 1
    if (-not $generatorLine) {
        return ""
    }

    return ($generatorLine -split "=", 2)[1]
}

function Show-Help {
    Write-Host "Usage: .\run-tests-cpp.ps1 [OPTIONS]"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  --debug, -d       Debug build"
    Write-Host "  --no-vulkan       Disable Vulkan support"
    Write-Host "  --vulkan          Enable Vulkan support (default)"
    Write-Host "  --no-sdl          Disable SDL2 support"
    Write-Host "  --sdl             Enable SDL2 support (default)"
    Write-Host "  --ccache          Use ccache if available (default)"
    Write-Host "  --no-ccache       Disable ccache compiler launcher"
    Write-Host "  --ninja           Use Ninja generator for a new build dir"
    Write-Host "  --unity           Enable CMake unity build (experimental)"
    Write-Host "  --no-unity        Disable CMake unity build (default)"
    Write-Host "  --pch             Enable precompiled headers for selected C++ targets (default)"
    Write-Host "  --no-pch          Disable precompiled headers"
    Write-Host "  --window-tests    Build and run tests that create windows/GL contexts"
    Write-Host "  --no-window-tests Disable tests that require a windowing system"
    Write-Host "  --help, -h        Show this help"
    Write-Host ""
    Write-Host "Environment:"
    Write-Host "  SDK_PREFIX        SDK prefix for installed dependencies (default: .\sdk)"
    Write-Host "  BUILD_DIR         CMake build directory (default: .\build\<BUILD_TYPE>-tests)"
    Write-Host "  BUILD_JOBS        Parallel build jobs (default: logical processor count)"
    Write-Host "  TERMIN_CMAKE_GENERATOR or CMAKE_GENERATOR_NAME"
    Write-Host "                    CMake generator for a new build dir (default: CMake default)"
}

foreach ($arg in $args) {
    switch ($arg) {
        "--debug"           { $BuildType = "Debug" }
        "-d"                { $BuildType = "Debug" }
        "--no-vulkan"       { $VulkanMode = "off" }
        "--vulkan"          { $VulkanMode = "on" }
        "--no-sdl"          { $SdlMode = "off" }
        "--sdl"             { $SdlMode = "on" }
        "--ccache"          { $CcacheMode = "on" }
        "--no-ccache"       { $CcacheMode = "off" }
        "--ninja"           { $CmakeGeneratorName = "Ninja" }
        "--unity"           { $UnityMode = "on" }
        "--no-unity"        { $UnityMode = "off" }
        "--pch"             { $PchMode = "on" }
        "--no-pch"          { $PchMode = "off" }
        "--window-tests"    { $WindowTestsMode = "on" }
        "--no-window-tests" { $WindowTestsMode = "off" }
        "--help"            { Show-Help; exit 0 }
        "-h"                { Show-Help; exit 0 }
        default             { Write-Error "Unknown option: $arg"; exit 1 }
    }
}

if (-not $BuildDir) {
    $BuildDir = Join-Path (Join-Path $ScriptDir "build") "$BuildType-tests"
}

$TerminEnableVulkan = if ($VulkanMode -eq "on") { "ON" } else { "OFF" }
$TerminEnableSdl = if ($SdlMode -eq "on") { "ON" } else { "OFF" }
$TerminUseCcache = if ($CcacheMode -eq "on") { "ON" } else { "OFF" }
$TerminEnableUnityBuild = if ($UnityMode -eq "on") { "ON" } else { "OFF" }
$TerminEnablePch = if ($PchMode -eq "on") { "ON" } else { "OFF" }

switch ($WindowTestsMode) {
    "off" { $TerminBuildWindowTests = "OFF" }
    "on"  { $TerminBuildWindowTests = "ON" }
    default {
        if ($env:DISPLAY -or $env:WAYLAND_DISPLAY) {
            $TerminBuildWindowTests = "ON"
        } else {
            $TerminBuildWindowTests = "OFF"
        }
    }
}

$pathEntries = @(
    (Join-Path $SdkPrefix "bin"),
    (Join-Path $SdkPrefix "lib"),
    (Join-Path $BuildDir "bin")
) | Where-Object { Test-Path $_ }
if ($pathEntries.Count -gt 0) {
    $env:PATH = ($pathEntries -join [IO.Path]::PathSeparator) + [IO.Path]::PathSeparator + $env:PATH
}

$ldEntries = @((Join-Path $SdkPrefix "lib"), (Join-Path $BuildDir "bin"))
if ($env:LD_LIBRARY_PATH) {
    $ldEntries += $env:LD_LIBRARY_PATH
}
$env:LD_LIBRARY_PATH = ($ldEntries -join [IO.Path]::PathSeparator)

Write-Host ""
Write-Host "========================================"
Write-Host "  C/C++ tests ($BuildType)"
Write-Host "  mode: top-level CMake graph"
Write-Host "========================================"
Write-Host ""
Write-Host "Source dir:  $ScriptDir"
Write-Host "Build dir:   $BuildDir"
Write-Host "SDK prefix:  $SdkPrefix"
Write-Host "Vulkan:      $TerminEnableVulkan"
Write-Host "SDL2:        $TerminEnableSdl"
Write-Host "Window tests:$TerminBuildWindowTests ($WindowTestsMode)"
Write-Host "ccache:      $TerminUseCcache"
Write-Host "Unity build: $TerminEnableUnityBuild"
Write-Host "PCH:         $TerminEnablePch"
Write-Host "Generator:   $(if ($CmakeGeneratorName) { $CmakeGeneratorName } else { 'existing/default' })"
Write-Host "Jobs:        $BuildJobs"
Write-Host ""

$requiredSubmodules = @(
    "termin-thirdparty/manifold",
    "termin-thirdparty/clipper2",
    "termin-thirdparty/guard",
    "termin-thirdparty/recastnavigation"
)
if ($TerminEnableVulkan -eq "ON") {
    $requiredSubmodules += "termin-thirdparty/vulkan-memory-allocator"
}
& (Join-Path $ScriptDir "scripts\Ensure-ThirdpartySubmodules.ps1") -RepoRoot $ScriptDir -RequiredPaths $requiredSubmodules

$cmakeArgs = @("-S", $ScriptDir, "-B", $BuildDir)
if ($CmakeGeneratorName -and -not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    $cmakeArgs += @("-G", $CmakeGeneratorName)
}
$cmakeArgs += @(
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_PREFIX_PATH=$SdkPrefix",
    "-DCMAKE_INSTALL_PREFIX=$SdkPrefix",
    "-DCMAKE_BUILD_RPATH=$((Join-Path $SdkPrefix 'lib'));$((Join-Path $BuildDir 'bin'))",
    "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
    "-DTERMIN_USE_CCACHE=$TerminUseCcache",
    "-DTERMIN_ENABLE_UNITY_BUILD=$TerminEnableUnityBuild",
    "-DTERMIN_ENABLE_PCH=$TerminEnablePch",
    "-DTERMIN_BUILD_PYTHON=OFF",
    "-DTERMIN_BUILD_TESTS=ON",
    "-DTERMIN_BUILD_TGFX2_TESTS=ON",
    "-DTERMIN_BUILD_WINDOW_TESTS=$TerminBuildWindowTests",
    "-DTERMIN_ENABLE_VULKAN=$TerminEnableVulkan",
    "-DTERMIN_ENABLE_SDL=$TerminEnableSdl",
    "-DTERMIN_BUILD_EDITOR_MINIMAL=OFF",
    "-DTERMIN_BUILD_LAUNCHER=OFF"
)

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configure failed"
    exit 1
}

$ActualGenerator = Get-CMakeGeneratorFromCache $BuildDir
if ($ActualGenerator -like "Visual Studio*") {
    Write-Host "Visual Studio generator detected; using MSBuild /m:1 to avoid parallel solution race"
    & cmake --build $BuildDir --config $BuildType -- /m:1
} else {
    & cmake --build $BuildDir --config $BuildType --parallel $BuildJobs
}
if ($LASTEXITCODE -ne 0) {
    Write-Error "C++ test build failed"
    exit 1
}

& ctest --test-dir $BuildDir -C $BuildType --output-on-failure
if ($LASTEXITCODE -ne 0) {
    Write-Error "C++ tests failed"
    exit 1
}

Write-Host ""
Write-Host "========================================"
Write-Host "  C/C++ tests finished"
Write-Host "========================================"
