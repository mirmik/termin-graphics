function Enable-TerminSdkInheritedPermissions {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SdkPrefix
    )

    $sitePackages = Join-Path $SdkPrefix "python\Lib\site-packages"
    if (-not (Test-Path $sitePackages -PathType Container)) {
        throw "Bundled Python site-packages directory is missing: $sitePackages"
    }

    # Files created by a restricted build process may carry a protected ACL
    # that grants access only to that process identity. Restore inheritance at
    # package boundaries so the completed SDK has the same access contract as
    # its installation directory. Descendants then inherit from their package
    # root without an expensive whole-tree ACL rewrite.
    $targets = @(
        Get-Item -LiteralPath $sitePackages -Force
        Get-ChildItem -LiteralPath $sitePackages -Force
    )
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

    Write-Host "SDK Python permissions: enabled inheritance on $normalized item(s)"
}
