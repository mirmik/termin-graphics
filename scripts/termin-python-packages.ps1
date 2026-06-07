$TerminPythonPackages = @(
    "termin-build-tools",
    "termin-nanobind-sdk",
    "termin-base",
    "termin-assets",
    "termin-mesh",
    "termin-graphics",
    "termin-materials",
    "termin-gui",
    "termin-inspect",
    "termin-scene",
    "termin-display",
    "termin-csg",
    "termin-modules",
    "termin-components/termin-components-kinematic",
    "termin-lighting",
    "termin-components/termin-components-mesh",
    "termin-input",
    "termin-collision",
    "termin-render",
    "termin-components/termin-components-render",
    "termin-components/termin-components-foliage",
    "termin-render-passes",
    "termin-navmesh",
    "termin-qopt",
    "termin-pga",
    "termin-physics",
    "termin-engine",
    "termin-skeleton",
    "termin-animation",
    "termin-nodegraph",
    "termin-app",
    "tcplot"
)

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
