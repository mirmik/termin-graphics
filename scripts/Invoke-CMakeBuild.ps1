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

    $buildArgs = @(
        "--build", $BuildDir,
        "--config", $BuildType
    )
    if ($Target.Count -gt 0) {
        $buildArgs += @("--target") + $Target
    }
    $buildArgs += @("--parallel", $BuildJobs)

    $actualGenerator = Get-TerminCMakeGenerator -BuildDir $BuildDir
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
