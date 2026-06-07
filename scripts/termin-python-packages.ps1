$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$PackageManifestPath = Join-Path $RepoRoot "build-system\packages.json"
if (-not (Test-Path $PackageManifestPath)) {
    throw "Package manifest not found: $PackageManifestPath"
}

$PackageManifest = Get-Content -LiteralPath $PackageManifestPath -Raw | ConvertFrom-Json
$TerminPythonPackages = @($PackageManifest.packages | ForEach-Object { $_.path })

function Clear-TerminPythonPackageBuildCaches {
    param([string]$RepoRoot)

    foreach ($pkg in $TerminPythonPackages) {
        $pkgDir = Join-Path $RepoRoot $pkg
        if (-not (Test-Path $pkgDir)) {
            continue
        }

        Get-ChildItem -Path (Join-Path $pkgDir "build") -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "lib.*" -or $_.Name -like "bdist.*" } |
            Remove-Item -Recurse -Force
        Get-ChildItem -Path $pkgDir -Directory -Filter "*.egg-info" -ErrorAction SilentlyContinue |
            Remove-Item -Recurse -Force
    }
}
