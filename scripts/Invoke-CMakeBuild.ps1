function Get-TerminCMakeGenerator {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir
    )

    $cachePath = Join-Path $BuildDir "CMakeCache.txt"
    if (-not (Test-Path $cachePath)) {
        return ""
    }

    $generatorLine = Get-Content $cachePath |
        Where-Object { $_ -like "CMAKE_GENERATOR:INTERNAL=*" } |
        Select-Object -First 1
    if (-not $generatorLine) {
        return ""
    }

    return ($generatorLine -split "=", 2)[1]
}

function Get-TerminMSBuildPath {
    $command = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $vswherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswherePath)) {
        throw "Visual Studio generator requires MSBuild.exe, but neither MSBuild.exe nor vswhere.exe was found"
    }

    $installationPath = (& $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1)
    if (-not $installationPath) {
        throw "vswhere.exe did not find a Visual Studio installation with MSBuild"
    }
    $msbuildPath = Join-Path $installationPath "MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path $msbuildPath)) {
        throw "Visual Studio reported by vswhere.exe has no MSBuild.exe at $msbuildPath"
    }
    return $msbuildPath
}

function Get-TerminVisualStudioSolution {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir
    )

    $solutions = @(Get-ChildItem -LiteralPath $BuildDir -File -Filter "*.sln")
    if ($solutions.Count -ne 1) {
        throw "Expected exactly one Visual Studio solution in $BuildDir, found $($solutions.Count)"
    }
    return $solutions[0].FullName
}

function Invoke-TerminCMakeBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir,

        [Parameter(Mandatory = $true)]
        [string]$BuildType,

        [string[]]$Target = @(),

        [int]$BuildJobs = [Environment]::ProcessorCount
    )

    if ($BuildJobs -lt 1) {
        throw "BuildJobs must be at least 1, got $BuildJobs"
    }

    $actualGenerator = Get-TerminCMakeGenerator -BuildDir $BuildDir
    if ($actualGenerator -like "Visual Studio*" -and $Target.Count -gt 1) {
        $msbuildPath = Get-TerminMSBuildPath
        $solutionPath = Get-TerminVisualStudioSolution -BuildDir $BuildDir
        $msbuildArgs = @(
            $solutionPath,
            "/t:$($Target -join ';')",
            "/p:Configuration=$BuildType",
            "/m:$BuildJobs",
            "/p:UseMultiToolTask=true",
            "/p:EnforceProcessCountAcrossBuilds=true",
            "/p:CL_MPCount=$BuildJobs"
        )
        Write-Host "Visual Studio solution graph build: $($Target.Count) targets, $BuildJobs shared jobs"
        & $msbuildPath @msbuildArgs
        if ($LASTEXITCODE -ne 0) {
            throw "MSBuild solution graph failed with exit code $LASTEXITCODE"
        }
        return
    }

    $buildArgs = @(
        "--build", $BuildDir,
        "--config", $BuildType
    )
    if ($Target.Count -gt 0) {
        $buildArgs += @("--target") + $Target
    }
    $buildArgs += @("--parallel", $BuildJobs)

    if ($actualGenerator -like "Visual Studio*" -and $BuildJobs -gt 1) {
        # CMake's --parallel maps to MSBuild /m and schedules independent
        # projects. MultiToolTask also schedules translation units inside a
        # vcxproj. EnforceProcessCountAcrossBuilds keeps both levels under the
        # same CL_MPCount budget instead of multiplying their process counts.
        $buildArgs += @(
            "--",
            "/p:UseMultiToolTask=true",
            "/p:EnforceProcessCountAcrossBuilds=true",
            "/p:CL_MPCount=$BuildJobs"
        )
        Write-Host "Visual Studio parallel build: $BuildJobs shared MSBuild/compiler jobs"
    } else {
        Write-Host "CMake parallel build: $BuildJobs job(s)"
    }

    & cmake @buildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "cmake build failed with exit code $LASTEXITCODE"
    }
}
