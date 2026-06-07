param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string[]]$RequiredPaths = @()
)

$ErrorActionPreference = "Stop"

$ExpectedFiles = @{
    "termin-thirdparty/manifold" = @("CMakeLists.txt")
    "termin-thirdparty/clipper2" = @("CPP/CMakeLists.txt")
    "termin-thirdparty/guard" = @("guard_c.h", "guard_main.h")
    "termin-thirdparty/vulkan-memory-allocator" = @("include/vk_mem_alloc.h")
    "termin-thirdparty/openxr-sdk" = @("include/openxr/openxr.h")
    "termin-thirdparty/recastnavigation" = @("Recast/CMakeLists.txt", "Detour/CMakeLists.txt")
}

function Test-SubmoduleReady {
    param([string]$RelativePath)

    $fullPath = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path $fullPath)) {
        return $false
    }

    if ($ExpectedFiles.ContainsKey($RelativePath)) {
        foreach ($file in $ExpectedFiles[$RelativePath]) {
            if (-not (Test-Path (Join-Path $fullPath $file))) {
                return $false
            }
        }
        return $true
    }

    return [bool](Get-ChildItem -LiteralPath $fullPath -Force -ErrorAction SilentlyContinue | Select-Object -First 1)
}

$normalizedPaths = @(
    foreach ($path in $RequiredPaths) {
        if ($path) {
            $path.Replace("\", "/")
        }
    }
) | Select-Object -Unique

$missing = @($normalizedPaths | Where-Object { -not (Test-SubmoduleReady $_) })
if ($missing.Count -eq 0) {
    return
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Required git submodules are missing and git was not found: $($missing -join ', ')"
}

Write-Host "Initializing missing third-party submodules:"
foreach ($path in $missing) {
    Write-Host "  - $path"
}

Push-Location $RepoRoot
try {
    & git submodule update --init --recursive -- @missing
    if ($LASTEXITCODE -ne 0) {
        throw "git submodule update failed"
    }
}
finally {
    Pop-Location
}

$stillMissing = @($normalizedPaths | Where-Object { -not (Test-SubmoduleReady $_) })
if ($stillMissing.Count -gt 0) {
    throw "Required git submodules are still missing after initialization: $($stillMissing -join ', ')"
}
