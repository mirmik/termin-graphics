#!/usr/bin/env pwsh
# Build the SDK using the dedicated stage scripts:
#   1. build-sdk-cpp.ps1        — C/C++ libraries
#   2. build-sdk-bindings.ps1   — Python bindings (nanobind)
#   3. build-sdk-csharp.ps1     — C# bindings
#   4. install-pip-packages.ps1 — populate bundled Python's site-packages
#
# To install pip packages into your own user Python environment, run separately:
#   .\install-pip-packages.ps1

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

foreach ($arg in $args) {
    if ($arg -eq "--help" -or $arg -eq "-h") {
        Write-Host "Usage: .\build-sdk.ps1 [OPTIONS]"
        Write-Host ""
        Write-Host "Options:"
        Write-Host "  --debug, -d       Debug build"
        Write-Host "  --clean, -c       Clean build directories first"
        Write-Host "  --no-parallel     Disable parallel compilation"
        Write-Host "  --ccache          Use ccache if available (default; ignored by MSVC root graph)"
        Write-Host "  --no-ccache       Disable ccache compiler launcher"
        Write-Host "  --ninja           Use Ninja generator for a new build dir"
        Write-Host "  --unity           Enable CMake unity build for C/C++ stages (experimental)"
        Write-Host "  --no-unity        Disable CMake unity build (default)"
        Write-Host "  --pch             Enable precompiled headers for C/C++ stages (default)"
        Write-Host "  --no-pch          Disable precompiled headers"
        Write-Host "  --no-vulkan       Disable Vulkan support"
        Write-Host "  --vulkan          Require Vulkan support"
        Write-Host "  --no-sdl          Disable SDL2 support"
        Write-Host "  --sdl             Force SDL2 support on"
        Write-Host "  --no-opengl       Disable OpenGL backend; keep Vulkan render/editor targets"
        Write-Host "  --opengl          Enable desktop OpenGL targets (default)"
        Write-Host "  --help, -h        Show this help"
        exit 0
    }
}

Write-Host ""
Write-Host "========================================"
Write-Host "  Stage 1/4: C/C++ libraries"
Write-Host "========================================"
Write-Host ""
& (Join-Path $ScriptDir "build-sdk-cpp.ps1") @args
if ($LASTEXITCODE -ne 0) { throw "C++ build failed" }

Write-Host ""
Write-Host "========================================"
Write-Host "  Stage 2/4: Python bindings (nanobind)"
Write-Host "========================================"
Write-Host ""
& (Join-Path $ScriptDir "build-sdk-bindings.ps1") @args
if ($LASTEXITCODE -ne 0) { throw "Bindings build failed" }

Write-Host ""
Write-Host "========================================"
Write-Host "  Stage 3/4: C# bindings"
Write-Host "========================================"
Write-Host ""
& (Join-Path $ScriptDir "build-sdk-csharp.ps1") @args
if ($LASTEXITCODE -ne 0) { throw "C# build failed" }

Write-Host ""
Write-Host "========================================"
Write-Host "  Stage 4/4: Populate bundled Python site-packages"
Write-Host "========================================"
Write-Host ""

function Get-HostPythonInfo {
    $json = & python -c "import json, site, sys, sysconfig; print(json.dumps({'executable': sys.executable, 'prefix': sys.prefix, 'stdlib': sysconfig.get_paths()['stdlib']}))"
    if ($LASTEXITCODE -ne 0) { throw "failed to inspect host Python" }
    return $json | ConvertFrom-Json
}

function Copy-TreeWithRobocopy {
    param(
        [string]$Source,
        [string]$Destination,
        [string[]]$ExcludeDirs = @(),
        [string[]]$ExcludeFiles = @()
    )

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $robocopyArgs = @($Source, $Destination, "/E", "/NFL", "/NDL", "/NJH", "/NJS", "/NP")
    if ($ExcludeDirs.Count -gt 0) {
        $robocopyArgs += "/XD"
        $robocopyArgs += $ExcludeDirs
    }
    if ($ExcludeFiles.Count -gt 0) {
        $robocopyArgs += "/XF"
        $robocopyArgs += $ExcludeFiles
    }

    & robocopy @robocopyArgs | Out-Null
    $code = $LASTEXITCODE
    if ($code -gt 7) {
        throw "robocopy failed from $Source to $Destination with exit code $code"
    }
    $global:LASTEXITCODE = 0
}

function Ensure-WindowsBundledPythonRuntime {
    param([string]$SdkPrefix)

    $pythonHome = Join-Path $SdkPrefix "python"
    $stdlibTarget = Join-Path $pythonHome "Lib"
    if (Test-Path (Join-Path $stdlibTarget "os.py")) {
        return
    }

    $py = Get-HostPythonInfo
    if (-not (Test-Path $py.stdlib)) {
        throw "host Python stdlib not found: $($py.stdlib)"
    }

    Write-Host "Bundled Python stdlib not found; creating it from host Python."
    Write-Host "Host Python: $($py.executable)"
    Write-Host "Host stdlib: $($py.stdlib)"
    Write-Host "SDK Python home: $pythonHome"

    Copy-TreeWithRobocopy `
        -Source $py.stdlib `
        -Destination $stdlibTarget `
        -ExcludeDirs @("__pycache__", "test", "tests", "idle_test", "turtledemo", "lib2to3", "ensurepip", "site-packages") `
        -ExcludeFiles @("*.pyc", "*.pyo")

    $pythonDlls = Get-ChildItem -Path $py.prefix -Filter "python*.dll" -File -ErrorAction SilentlyContinue
    foreach ($dll in $pythonDlls) {
        Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $SdkPrefix "bin") -Force
        Copy-Item -LiteralPath $dll.FullName -Destination $pythonHome -Force
    }

    $hostDlls = Join-Path $py.prefix "DLLs"
    if (Test-Path $hostDlls) {
        Copy-TreeWithRobocopy -Source $hostDlls -Destination (Join-Path $pythonHome "DLLs") -ExcludeDirs @("__pycache__") -ExcludeFiles @("*.pyc", "*.pyo")
    }

    $hostTcl = Join-Path $py.prefix "tcl"
    if (Test-Path $hostTcl) {
        Copy-TreeWithRobocopy -Source $hostTcl -Destination (Join-Path $pythonHome "tcl") -ExcludeDirs @("__pycache__") -ExcludeFiles @("*.pyc", "*.pyo")
    }
}

function Grant-CurrentUserFullControl {
    param([string]$Path)

    if ($env:OS -ne "Windows_NT" -or -not (Test-Path $Path)) {
        return
    }

    $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
    $grant = "$identity`:(OI)(CI)F"
    & icacls $Path /inheritance:e /grant $grant /T /C | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "failed to grant current user access to $Path"
    }
}

function Reset-BundledSitePackages {
    param(
        [string]$SdkPrefix,
        [string]$SitePackages
    )

    $resolvedSdkPrefix = (Resolve-Path $SdkPrefix).Path
    $sitePackagesParent = Split-Path -Parent $SitePackages
    if (-not (Test-Path $sitePackagesParent)) {
        New-Item -ItemType Directory -Path $sitePackagesParent -Force | Out-Null
    }

    if (Test-Path $SitePackages) {
        $resolvedSitePackages = (Resolve-Path $SitePackages).Path
        if (-not $resolvedSitePackages.StartsWith($resolvedSdkPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove bundled site-packages outside SDK: $resolvedSitePackages"
        }
        Grant-CurrentUserFullControl $resolvedSitePackages
        Remove-Item -LiteralPath $resolvedSitePackages -Recurse -Force
    }

    New-Item -ItemType Directory -Path $SitePackages -Force | Out-Null
}

# Resolve the Python version used by the bundled interpreter. Linux installs
# the stdlib under sdk/lib/python<MAJOR>.<MINOR>/; Windows uses sdk/python/Lib/.
$SdkPrefix = if ($env:SDK_PREFIX) { $env:SDK_PREFIX } else { Join-Path $ScriptDir "sdk" }
if ($env:OS -eq "Windows_NT") {
    Ensure-WindowsBundledPythonRuntime $SdkPrefix
}
$WindowsBundledLib = Join-Path (Join-Path $SdkPrefix "python") "Lib"
$LinuxBundledPyDir = Get-ChildItem -Path (Join-Path $SdkPrefix "lib") -Directory -Filter "python3.*" -ErrorAction SilentlyContinue | Select-Object -First 1

if (Test-Path $WindowsBundledLib) {
    $BundledPyDir = Get-Item $WindowsBundledLib
    $BundledSitePackages = Join-Path $BundledPyDir.FullName "site-packages"
} elseif ($LinuxBundledPyDir) {
    $BundledPyDir = $LinuxBundledPyDir
    $BundledSitePackages = Join-Path $BundledPyDir.FullName "site-packages"
} else {
    Write-Host "WARNING: bundled Python stdlib not found under $SdkPrefix\python\Lib or $SdkPrefix\lib\python3.*" -ForegroundColor Yellow
    Write-Host "  Skipping pip install into bundled site-packages."
    Write-Host "  Was BUNDLE_PYTHON=ON during the termin CMake build?"
}

if ($BundledSitePackages) {
    Write-Host "Bundled Python stdlib:        $($BundledPyDir.FullName)"
    Write-Host "Bundled Python site-packages: $BundledSitePackages"
    Write-Host ""

    $env:TERMIN_SDK = $SdkPrefix

    Reset-BundledSitePackages -SdkPrefix $SdkPrefix -SitePackages $BundledSitePackages

    Write-Host "Installing external Python runtime dependencies..."
    & python -m pip install --upgrade --target $BundledSitePackages -r (Join-Path (Join-Path $ScriptDir "termin-app") "requirements.txt")
    if ($LASTEXITCODE -ne 0) { throw "runtime dependency install into bundled site-packages failed" }

    # --force bypasses pip's wheel cache: build-sdk.ps1 can rebuild the
    # native .pyd files without changing the package version string, and
    # pip would then happily reuse a stale wheel.
    & (Join-Path $ScriptDir "install-pip-packages.ps1") --force --target $BundledSitePackages
    if ($LASTEXITCODE -ne 0) { throw "pip install into bundled site-packages failed" }

    Grant-CurrentUserFullControl $BundledSitePackages

    $ResolvedSdkPrefix = (Resolve-Path $SdkPrefix).Path
    $LegacyPythonTrees = @(
        (Join-Path (Join-Path $SdkPrefix "lib") "python"),
        (Join-Path (Join-Path $SdkPrefix "lib") "site-packages")
    )

    foreach ($LegacyPythonTree in $LegacyPythonTrees) {
        if (-not (Test-Path $LegacyPythonTree)) {
            continue
        }
        $ResolvedLegacyTree = (Resolve-Path $LegacyPythonTree).Path
        if (-not $ResolvedLegacyTree.StartsWith($ResolvedSdkPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove legacy Python tree outside SDK: $ResolvedLegacyTree"
        }
        Write-Host "Removing legacy SDK Python staging tree: $ResolvedLegacyTree"
        Remove-Item -LiteralPath $ResolvedLegacyTree -Recurse -Force
    }
}

Write-Host ""
Write-Host "========================================"
Write-Host "  All done!"
Write-Host "========================================"
