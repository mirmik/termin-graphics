#!/usr/bin/env pwsh
# Build and install Python/nanobind bindings through the top-level CMake graph.
# This mirrors build-sdk-bindings.sh for Windows/PowerShell.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "scripts\Normalize-WindowsBuildEnvironment.ps1")
. (Join-Path $ScriptDir "scripts\Invoke-CMakeBuild.ps1")
Normalize-WindowsBuildEnvironment

$SdkPrefix = if ($env:SDK_PREFIX) { $env:SDK_PREFIX } else { Join-Path $ScriptDir "sdk" }
$BuildDirEnv = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { $null }

$BuildType = "Release"
$Clean = $false
$NoParallel = $false
$VulkanMode = "auto"
$SdlMode = "on"
$OpenGlMode = "on"
$CcacheMode = "on"
$UnityMode = "off"
$PchMode = "on"
$Profile = "full"
$BuildJobs = if ($env:BUILD_JOBS) { [int]$env:BUILD_JOBS } else { [Environment]::ProcessorCount }
$CmakeGeneratorName = if ($env:CMAKE_GENERATOR_NAME) { $env:CMAKE_GENERATOR_NAME } elseif ($env:TERMIN_CMAKE_GENERATOR) { $env:TERMIN_CMAKE_GENERATOR } else { $null }

function Show-Help {
    Write-Host "Usage: .\build-sdk-bindings.ps1 [OPTIONS]"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  --debug, -d       Debug build"
    Write-Host "  --clean, -c       Clean build directory first"
    Write-Host "  --no-parallel     Disable parallel compilation (equivalent to -j1)"
    Write-Host "  --ccache          Use ccache if available (default; ignored by MSVC root graph)"
    Write-Host "  --no-ccache       Disable ccache compiler launcher"
    Write-Host "  --ninja           Use Ninja generator for a new build dir"
    Write-Host "  --unity           Enable CMake unity build (experimental)"
    Write-Host "  --no-unity        Disable CMake unity build (default)"
    Write-Host "  --pch             Enable precompiled headers for selected C++ targets (default)"
    Write-Host "  --no-pch          Disable precompiled headers"
    Write-Host "  --no-vulkan       Disable Vulkan support"
    Write-Host "  --vulkan          Require Vulkan support"
    Write-Host "  --no-sdl          Disable SDL2 support"
    Write-Host "  --sdl             Enable SDL2 support (default)"
    Write-Host "  --no-opengl       Disable OpenGL backend; keep Vulkan render/editor targets"
    Write-Host "  --opengl          Enable desktop OpenGL targets (default)"
    Write-Host "  --profile=NAME    SDK graph profile: full (default), graphics, or core"
    Write-Host "  --help, -h        Show this help"
    Write-Host ""
    Write-Host "Environment:"
    Write-Host "  SDK_PREFIX        Install prefix (default: .\sdk)"
    Write-Host "  BUILD_DIR         CMake build directory (default: .\build\<BUILD_TYPE>)"
    Write-Host "  BUILD_JOBS        Parallel build jobs (default: logical processor count)"
    Write-Host "  TERMIN_CMAKE_GENERATOR or CMAKE_GENERATOR_NAME"
    Write-Host "                    CMake generator for a new build dir (default: CMake default)"
}

function Test-VulkanSdkAvailable {
    if (-not $env:VULKAN_SDK) {
        return $false
    }

    return (Test-Path (Join-Path $env:VULKAN_SDK "Include\vulkan\vulkan.h")) -and
        (Test-Path (Join-Path $env:VULKAN_SDK "Lib\vulkan-1.lib"))
}

foreach ($arg in $args) {
    if ($arg -like "--profile=*") {
        $Profile = $arg.Substring("--profile=".Length)
        continue
    }
    switch ($arg) {
        "--debug"       { $BuildType = "Debug" }
        "-d"            { $BuildType = "Debug" }
        "--clean"       { $Clean = $true }
        "-c"            { $Clean = $true }
        "--no-parallel" { $NoParallel = $true }
        "--ccache"      { $CcacheMode = "on" }
        "--no-ccache"   { $CcacheMode = "off" }
        "--ninja"       { $CmakeGeneratorName = "Ninja" }
        "--unity"       { $UnityMode = "on" }
        "--no-unity"    { $UnityMode = "off" }
        "--pch"         { $PchMode = "on" }
        "--no-pch"      { $PchMode = "off" }
        "--no-vulkan"   { $VulkanMode = "off" }
        "--vulkan"      { $VulkanMode = "on" }
        "--no-sdl"      { $SdlMode = "off" }
        "--sdl"         { $SdlMode = "on" }
        "--no-opengl"   { $OpenGlMode = "off" }
        "--opengl"      { $OpenGlMode = "on" }
        "--help"        { Show-Help; exit 0 }
        "-h"            { Show-Help; exit 0 }
        default          { Write-Error "Unknown option: $arg"; exit 1 }
    }
}

if ($Profile -notin @("full", "graphics", "core")) {
    throw "Unsupported SDK profile: $Profile. Expected 'full', 'graphics', or 'core'."
}
$env:TERMIN_SDK_PROFILE = $Profile

if ($NoParallel) {
    $BuildJobs = 1
}

$DefaultBuildName = if ($Profile -eq "full") { $BuildType } else { "$BuildType-$Profile" }
$BuildDir = if ($BuildDirEnv) { $BuildDirEnv } else { Join-Path (Join-Path $ScriptDir "build") $DefaultBuildName }

switch ($VulkanMode) {
    "on" {
        $TerminEnableVulkan = "ON"
        $VulkanModeLabel = "ON"
    }
    "off" {
        $TerminEnableVulkan = "OFF"
        $VulkanModeLabel = "OFF"
    }
    default {
        if (Test-VulkanSdkAvailable) {
            $TerminEnableVulkan = "ON"
            $VulkanModeLabel = "ON (auto)"
        } else {
            $TerminEnableVulkan = "OFF"
            $VulkanModeLabel = "OFF (auto; Vulkan SDK not found)"
        }
    }
}
$TerminEnableSdl = if ($SdlMode -eq "on") { "ON" } else { "OFF" }
$TerminEnableOpenGl = if ($OpenGlMode -eq "on") { "ON" } else { "OFF" }
# Windows SDK consumers always include the D3D11 backend.  In particular, the
# C# stage packages the D3D11 shader set even when OpenGL and Vulkan are
# explicitly disabled, so D3D11 artifacts are part of the base Windows SDK
# contract rather than an OpenGL side effect.
$TerminBuildBuiltinShaderArtifacts = "ON"
$TerminBuiltinShaderArtifactTargets = if ($TerminEnableOpenGl -eq "ON") { "d3d11;opengl330" } else { "d3d11" }
$TerminUseCcache = if ($CcacheMode -eq "on") { "ON" } else { "OFF" }
$TerminEnableUnityBuild = if ($UnityMode -eq "on") { "ON" } else { "OFF" }
$TerminEnablePch = if ($PchMode -eq "on") { "ON" } else { "OFF" }

$pythonCommand = $null
if ($env:PYTHON_BIN) {
    $pythonCommand = Get-Command $env:PYTHON_BIN -ErrorAction SilentlyContinue
}
if (-not $pythonCommand -and $env:PYTHON_EXECUTABLE) {
    $pythonCommand = Get-Command $env:PYTHON_EXECUTABLE -ErrorAction SilentlyContinue
}
if (-not $pythonCommand) {
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
}
if (-not $pythonCommand) {
    $pythonCommand = Get-Command python3 -ErrorAction SilentlyContinue
}
if (-not $pythonCommand) {
    throw "Python executable not found in PATH"
}
$pythonExec = $pythonCommand.Source

$oldPythonPath = $env:PYTHONPATH
$env:PYTHONPATH = (Join-Path $ScriptDir "termin-build-tools")
if ($oldPythonPath) {
    $env:PYTHONPATH = "$env:PYTHONPATH$([IO.Path]::PathSeparator)$oldPythonPath"
}

$DoctorProfile = switch ($Profile) {
    "graphics" { "sdk-bindings-graphics" }
    "core" { "sdk-bindings-core" }
    default { "sdk-bindings" }
}
& $pythonExec -m termin_build.sdk --repo-root $ScriptDir doctor --profile $DoctorProfile --vulkan $TerminEnableVulkan --sdl $TerminEnableSdl --init-submodules
if ($LASTEXITCODE -ne 0) { throw "SDK bindings preflight failed" }

Write-Host ""
Write-Host "========================================"
Write-Host "  Building Termin Python bindings ($BuildType)"
Write-Host "  mode: top-level CMake graph"
Write-Host "========================================"
Write-Host ""
Write-Host "Source dir:  $ScriptDir"
Write-Host "Build dir:   $BuildDir"
Write-Host "SDK prefix:  $SdkPrefix"
Write-Host "Python:      $pythonExec"
Write-Host "Vulkan:      $VulkanModeLabel"
Write-Host "SDL2:        $TerminEnableSdl"
Write-Host "OpenGL:      $TerminEnableOpenGl"
Write-Host "Shaders:     $(if ($TerminBuiltinShaderArtifactTargets) { $TerminBuiltinShaderArtifactTargets } else { 'source-only' })"
Write-Host "ccache:      $TerminUseCcache"
Write-Host "Unity build: $TerminEnableUnityBuild"
Write-Host "PCH:         $TerminEnablePch"
Write-Host "SDK profile: $Profile"
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
    "-DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON",
    "-DTERMIN_USE_CCACHE=$TerminUseCcache",
    "-DTERMIN_ENABLE_UNITY_BUILD=$TerminEnableUnityBuild",
    "-DTERMIN_ENABLE_PCH=$TerminEnablePch",
    "-DTERMIN_SDK_PROFILE=$Profile",
    "-DTERMIN_BUILD_PYTHON=ON",
    "-DTERMIN_BUILD_TESTS=OFF",
    "-DTERMIN_ENABLE_VULKAN=$TerminEnableVulkan",
    "-DTERMIN_ENABLE_SDL=$TerminEnableSdl",
    "-DTERMIN_ENABLE_OPENGL=$TerminEnableOpenGl",
    "-DTERMIN_BUILD_BUILTIN_SHADER_ARTIFACTS=$TerminBuildBuiltinShaderArtifacts",
    "-DTERMIN_BUILTIN_SHADER_ARTIFACT_TARGETS=$TerminBuiltinShaderArtifactTargets",
    "-DTERMIN_BUILD_EDITOR_MINIMAL=$(if ($Profile -eq 'full') { 'ON' } else { 'OFF' })",
    "-DTERMIN_BUILD_LAUNCHER=$(if ($Profile -eq 'full') { 'ON' } else { 'OFF' })",
    "-DPython_EXECUTABLE=$pythonExec"
)

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Invoke-TerminCMakeBuild -BuildDir $BuildDir -BuildType $BuildType -BuildJobs $BuildJobs

& cmake --install $BuildDir --config $BuildType
if ($LASTEXITCODE -ne 0) { throw "cmake install failed" }

& $pythonExec -m termin_build.sdk --repo-root $ScriptDir prepare-build-python-runtime --sdk-prefix $SdkPrefix
if ($LASTEXITCODE -ne 0) { throw "failed to prepare bundled Python runtime" }

& $pythonExec -m termin_build.sdk --repo-root $ScriptDir publish-cmake-python --install-dir $SdkPrefix --sdk-prefix $SdkPrefix
if ($LASTEXITCODE -ne 0) { throw "failed to publish CMake Python install" }

& $pythonExec -m termin_build.sdk --repo-root $ScriptDir write-artifacts --build-dir $BuildDir --sdk-prefix $SdkPrefix
if ($LASTEXITCODE -ne 0) { throw "failed to write SDK artifact manifest" }

Write-Host ""
Write-Host "========================================"
Write-Host "  Python bindings installed to $SdkPrefix"
Write-Host "========================================"
