function Normalize-WindowsBuildEnvironment {
    $isWindowsHost = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
        [System.Runtime.InteropServices.OSPlatform]::Windows)
    if (-not $isWindowsHost) {
        return
    }

    $envVars = [Environment]::GetEnvironmentVariables("Process")
    $pathNames = @(
        $envVars.Keys |
            Where-Object { [string]::Equals($_, "Path", [System.StringComparison]::OrdinalIgnoreCase) }
    )
    if ($pathNames.Count -le 1) {
        return
    }

    $orderedNames = @()
    if ($pathNames | Where-Object { [string]::Equals($_, "Path", [System.StringComparison]::Ordinal) }) {
        $orderedNames += "Path"
    }
    foreach ($name in $pathNames) {
        $alreadyOrdered = $false
        foreach ($orderedName in $orderedNames) {
            if ([string]::Equals($orderedName, $name, [System.StringComparison]::Ordinal)) {
                $alreadyOrdered = $true
                break
            }
        }
        if (-not $alreadyOrdered) {
            $orderedNames += $name
        }
    }

    $seen = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    $segments = [System.Collections.Generic.List[string]]::new()
    foreach ($name in $orderedNames) {
        $value = [string]$envVars[$name]
        if (-not $value) {
            continue
        }
        foreach ($segment in $value.Split([IO.Path]::PathSeparator)) {
            if (-not $segment) {
                continue
            }
            if ($seen.Add($segment)) {
                $segments.Add($segment)
            }
        }
    }

    foreach ($name in $pathNames) {
        Remove-Item -LiteralPath "Env:\$name" -ErrorAction SilentlyContinue
    }
    $env:Path = $segments -join [IO.Path]::PathSeparator

    Write-Host "Normalized duplicate PATH/Path process environment entries for MSBuild."
}
