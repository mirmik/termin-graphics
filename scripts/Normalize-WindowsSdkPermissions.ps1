function Enable-TerminInheritedPermissions {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$LiteralPath,

        [switch]$Recurse,

        [string]$Context = "generated tree"
    )

    $isWindowsHost = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
        [System.Runtime.InteropServices.OSPlatform]::Windows)
    if (-not $isWindowsHost) {
        return
    }

    $targets = [System.Collections.Generic.List[System.IO.FileSystemInfo]]::new()
    foreach ($path in $LiteralPath) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "$Context path is missing: $path"
        }
        $targets.Add((Get-Item -LiteralPath $path -Force))
        if ($Recurse) {
            foreach ($child in Get-ChildItem -LiteralPath $path -Force -Recurse) {
                $targets.Add($child)
            }
        }
    }

    # Python tempfile.mkdtemp() creates mode-0700 directories on modern
    # Windows Python. When it runs under a restricted build identity, those
    # directories carry a protected ACL and cannot later be cleaned by the
    # interactive checkout owner. Re-enable inheritance without removing the
    # creator's explicit rules.
    $sections = [System.Security.AccessControl.AccessControlSections]::Access
    $normalized = 0
    foreach ($target in $targets) {
        if ($PSVersionTable.PSEdition -eq "Core") {
            $acl = [System.IO.FileSystemAclExtensions]::GetAccessControl(
                $target,
                $sections
            )
        } else {
            $acl = $target.GetAccessControl($sections)
        }
        if (-not $acl.AreAccessRulesProtected) {
            continue
        }
        $acl.SetAccessRuleProtection($false, $true)
        if ($PSVersionTable.PSEdition -eq "Core") {
            [System.IO.FileSystemAclExtensions]::SetAccessControl($target, $acl)
        } else {
            $target.SetAccessControl($acl)
        }
        $normalized++
    }

    Write-Host "$Context permissions: enabled inheritance on $normalized item(s)"
}
function Enable-TerminSdkInheritedPermissions {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SdkPrefix
    )

    $sitePackages = Join-Path $SdkPrefix "python\Lib\site-packages"
    if (-not (Test-Path $sitePackages -PathType Container)) {
        throw "Bundled Python site-packages directory is missing: $sitePackages"
    }

    # Package boundaries are sufficient here: descendants inherit from their
    # package root, avoiding an expensive whole-SDK ACL traversal.
    $packageBoundaries = @(
        $sitePackages
        Get-ChildItem -LiteralPath $sitePackages -Force | ForEach-Object FullName
    )
    Enable-TerminInheritedPermissions `
        -LiteralPath $packageBoundaries `
        -Context "SDK Python"
}
