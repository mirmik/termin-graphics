#!/usr/bin/env pwsh
# Shared Windows implementation for the Android-family APK wrappers.

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("android", "quest-openxr")]
    [string]$Product,

    [string[]]$Arguments = @()
)

$ErrorActionPreference = "Stop"

$TerminRoot = Split-Path -Parent $PSScriptRoot
$IsQuest = $Product -eq "quest-openxr"
$PlatformDir = Join-Path $TerminRoot $(if ($IsQuest) { "termin-openxr\platform" } else { "termin-android\platform" })
$GradleBuildRoot = Join-Path $TerminRoot $(if ($IsQuest) { "build\android-gradle-openxr" } else { "build\android-gradle" })
$HostPython = if ($env:TERMIN_HOST_PYTHON) {
    $env:TERMIN_HOST_PYTHON
} else {
    Join-Path $TerminRoot "sdk\bin\termin_python.exe"
}
$ShaderCompiler = if ($env:TERMIN_SHADERC) {
    $env:TERMIN_SHADERC
} else {
    $null
}

$Abi = if ($env:ANDROID_ABI) { $env:ANDROID_ABI } else { "arm64-v8a" }
$Platform = if ($env:ANDROID_PLATFORM) { $env:ANDROID_PLATFORM } else { "android-26" }
$AndroidSdkRoot = if ($env:TERMIN_ANDROID_SDK_ROOT) {
    $env:TERMIN_ANDROID_SDK_ROOT
} else {
    $null
}
$SystemAndroidSdkRoot = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } elseif ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else { $null }
$AndroidNdkRoot = if ($env:ANDROID_NDK_HOME) { $env:ANDROID_NDK_HOME } elseif ($env:ANDROID_NDK_ROOT) { $env:ANDROID_NDK_ROOT } else { $null }
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { $null }
$NdkVersion = if ($env:TERMIN_ANDROID_NDK_VERSION) {
    $env:TERMIN_ANDROID_NDK_VERSION
} else {
    "27.2.12479018"
}
$AssetsDir = if ($IsQuest) {
    if ($env:TERMIN_OPENXR_ASSETS_DIR) {
        $env:TERMIN_OPENXR_ASSETS_DIR
    } else {
        Join-Path $TerminRoot "termin-android\assets"
    }
} elseif ($env:TERMIN_ANDROID_ASSETS_DIR) {
    $env:TERMIN_ANDROID_ASSETS_DIR
} else {
    Join-Path $TerminRoot "termin-android\assets"
}
$ApplicationId = if ($env:TERMIN_ANDROID_APPLICATION_ID) {
    $env:TERMIN_ANDROID_APPLICATION_ID
} elseif ($IsQuest) {
    ""
} else {
    "org.termin.android"
}
$AppLabel = if ($env:TERMIN_ANDROID_APP_LABEL) {
    $env:TERMIN_ANDROID_APP_LABEL
} elseif ($IsQuest) {
    "Termin OpenXR"
} else {
    "Termin Android"
}
$VersionCode = if ($env:TERMIN_ANDROID_VERSION_CODE) { $env:TERMIN_ANDROID_VERSION_CODE } else { "1" }
$VersionName = if ($env:TERMIN_ANDROID_VERSION_NAME) { $env:TERMIN_ANDROID_VERSION_NAME } else { "0.1.0" }
$GradleBin = if ($env:GRADLE_BIN) { $env:GRADLE_BIN } else { $null }
$AdbBin = if ($env:ADB) { $env:ADB } else { $null }
$Variant = "debug"
$InstallApk = $false
$LaunchOpenXR = $false
$ShowHelp = $false

function Require-OptionValue {
    param(
        [string]$Option,
        [int]$Index
    )

    if ($Index + 1 -ge $Arguments.Count) {
        throw "Option $Option requires a value."
    }
    return $Arguments[$Index + 1]
}

$index = 0
while ($index -lt $Arguments.Count) {
    $argument = $Arguments[$index]
    $consumedValue = $false
    switch -Regex ($argument) {
        "^--abi$" {
            $Abi = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--abi=(.*)$" {
            $Abi = $Matches[1]
            break
        }
        "^--platform$" {
            $Platform = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--platform=(.*)$" {
            $Platform = $Matches[1]
            break
        }
        "^--sdk-root$" {
            $AndroidSdkRoot = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--sdk-root=(.*)$" {
            $AndroidSdkRoot = $Matches[1]
            break
        }
        "^--ndk-version$" {
            $NdkVersion = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--ndk-version=(.*)$" {
            $NdkVersion = $Matches[1]
            break
        }
        "^--ndk-root$" {
            $AndroidNdkRoot = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--ndk-root=(.*)$" {
            $AndroidNdkRoot = $Matches[1]
            break
        }
        "^--android-home$" {
            $SystemAndroidSdkRoot = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--android-home=(.*)$" {
            $SystemAndroidSdkRoot = $Matches[1]
            break
        }
        "^--java-home$" {
            $JavaHome = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--java-home=(.*)$" {
            $JavaHome = $Matches[1]
            break
        }
        "^--gradle$" {
            $GradleBin = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--gradle=(.*)$" {
            $GradleBin = $Matches[1]
            break
        }
        "^--shader-compiler$" {
            $ShaderCompiler = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--shader-compiler=(.*)$" {
            $ShaderCompiler = $Matches[1]
            break
        }
        "^--assets-dir$" {
            $AssetsDir = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--assets-dir=(.*)$" {
            $AssetsDir = $Matches[1]
            break
        }
        "^--application-id$" {
            $ApplicationId = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--application-id=(.*)$" {
            $ApplicationId = $Matches[1]
            break
        }
        "^--app-label$" {
            $AppLabel = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--app-label=(.*)$" {
            $AppLabel = $Matches[1]
            break
        }
        "^--version-code$" {
            $VersionCode = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--version-code=(.*)$" {
            $VersionCode = $Matches[1]
            break
        }
        "^--version-name$" {
            $VersionName = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--version-name=(.*)$" {
            $VersionName = $Matches[1]
            break
        }
        "^--variant$" {
            $Variant = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--variant=(.*)$" {
            $Variant = $Matches[1]
            break
        }
        "^--adb$" {
            if (-not $IsQuest) {
                throw "Unknown option for Android APK build: $argument"
            }
            $AdbBin = Require-OptionValue $argument $index
            $consumedValue = $true
            break
        }
        "^--adb=(.*)$" {
            if (-not $IsQuest) {
                throw "Unknown option for Android APK build: $argument"
            }
            $AdbBin = $Matches[1]
            break
        }
        "^--install$" {
            if (-not $IsQuest) {
                throw "Unknown option for Android APK build: $argument"
            }
            $InstallApk = $true
            break
        }
        "^--launch$" {
            if (-not $IsQuest) {
                throw "Unknown option for Android APK build: $argument"
            }
            $InstallApk = $true
            $LaunchOpenXR = $true
            break
        }
        "^--help$|^-h$" {
            $ShowHelp = $true
            break
        }
        default {
            throw "Unknown option: $argument"
        }
    }
    if ($consumedValue) {
        $index += 1
    }
    $index += 1
}

if ($ShowHelp) {
    $productName = if ($IsQuest) { "Quest/OpenXR" } else { "Android" }
    Write-Output "Usage: build-$Product-apk.ps1 [OPTIONS]"
    Write-Output ""
    Write-Output "Build a Termin $productName APK."
    Write-Output ""
    Write-Output "  --abi ABI"
    Write-Output "  --platform API"
    Write-Output "  --sdk-root DIR"
    Write-Output "  --ndk-version VER"
    Write-Output "  --ndk-root DIR"
    Write-Output "  --android-home DIR"
    Write-Output "  --java-home DIR"
    Write-Output "  --assets-dir DIR"
    Write-Output "  --application-id ID"
    Write-Output "  --app-label LABEL"
    Write-Output "  --version-code CODE"
    Write-Output "  --version-name NAME"
    Write-Output "  --gradle PATH"
    Write-Output "  --shader-compiler PATH"
    Write-Output "  --variant debug|release"
    if ($IsQuest) {
        Write-Output "  --adb PATH"
        Write-Output "  --install"
        Write-Output "  --launch"
    }
    Write-Output "  --help, -h"
    exit 0
}

$SettingsRoot = if ($env:APPDATA) { $env:APPDATA } else { Join-Path $HOME "AppData\Roaming" }
$SettingsPath = Join-Path $SettingsRoot "termin\settings.json"
$TerminSettings = $null
if (Test-Path -LiteralPath $SettingsPath -PathType Leaf) {
    try {
        $TerminSettings = Get-Content -LiteralPath $SettingsPath -Raw | ConvertFrom-Json
    } catch {
        Write-Warning "Failed to read Termin user settings '$SettingsPath': $_"
    }
}

function Get-BuildSetting {
    param([string]$Name)
    if ($null -eq $TerminSettings -or $null -eq $TerminSettings.Build) {
        return $null
    }
    $Property = $TerminSettings.Build.PSObject.Properties[$Name]
    if ($null -eq $Property -or [string]::IsNullOrWhiteSpace([string]$Property.Value)) {
        return $null
    }
    return [string]$Property.Value
}

if ([string]::IsNullOrWhiteSpace($AndroidSdkRoot)) { $AndroidSdkRoot = Get-BuildSetting "androidSdkRoot" }
if ([string]::IsNullOrWhiteSpace($SystemAndroidSdkRoot)) { $SystemAndroidSdkRoot = Get-BuildSetting "androidHome" }
if ([string]::IsNullOrWhiteSpace($AndroidNdkRoot)) { $AndroidNdkRoot = Get-BuildSetting "androidNdkRoot" }
if ([string]::IsNullOrWhiteSpace($JavaHome)) { $JavaHome = Get-BuildSetting "javaHome" }
if ([string]::IsNullOrWhiteSpace($GradleBin)) { $GradleBin = Get-BuildSetting "gradle" }
if ([string]::IsNullOrWhiteSpace($ShaderCompiler)) { $ShaderCompiler = Get-BuildSetting "shaderCompiler" }
if ([string]::IsNullOrWhiteSpace($AdbBin)) { $AdbBin = Get-BuildSetting "adb" }

if ([string]::IsNullOrWhiteSpace($AndroidSdkRoot)) { $AndroidSdkRoot = Join-Path $TerminRoot "sdk\android" }
if ([string]::IsNullOrWhiteSpace($GradleBin)) { $GradleBin = "gradle" }
if ([string]::IsNullOrWhiteSpace($ShaderCompiler)) { $ShaderCompiler = Join-Path $TerminRoot "sdk\bin\termin_shaderc.exe" }
if ([string]::IsNullOrWhiteSpace($AdbBin) -and -not [string]::IsNullOrWhiteSpace($SystemAndroidSdkRoot)) {
    $SdkAdb = Join-Path $SystemAndroidSdkRoot "platform-tools\adb.exe"
    if (Test-Path -LiteralPath $SdkAdb -PathType Leaf) { $AdbBin = $SdkAdb }
}
if ([string]::IsNullOrWhiteSpace($AdbBin)) { $AdbBin = "adb" }
if ([string]::IsNullOrWhiteSpace($SystemAndroidSdkRoot)) {
    throw "Google Android SDK location is not configured. Set ANDROID_HOME/ANDROID_SDK_ROOT, pass --android-home, or configure Build/androidHome."
}
if (-not (Test-Path -LiteralPath (Join-Path $SystemAndroidSdkRoot "platforms") -PathType Container)) {
    throw "Google Android SDK has no platforms directory: $SystemAndroidSdkRoot"
}
$env:ANDROID_HOME = $SystemAndroidSdkRoot
$env:ANDROID_SDK_ROOT = $SystemAndroidSdkRoot
if (-not [string]::IsNullOrWhiteSpace($AndroidNdkRoot) -and -not (Test-Path -LiteralPath (Join-Path $AndroidNdkRoot "build\cmake\android.toolchain.cmake") -PathType Leaf)) {
    throw "Android NDK has no CMake toolchain: $AndroidNdkRoot"
}
if (-not [string]::IsNullOrWhiteSpace($JavaHome)) {
    $JavaExecutable = Join-Path $JavaHome "bin\java.exe"
    if (-not (Test-Path -LiteralPath $JavaExecutable -PathType Leaf)) {
        throw "Java home has no executable bin/java.exe: $JavaHome"
    }
    $env:JAVA_HOME = $JavaHome
}

$GradleTask = switch ($Variant) {
    "debug" { "assembleDebug" }
    "release" { "assembleRelease" }
    default { throw "Unsupported Android variant: $Variant (expected debug or release)." }
}

if ($IsQuest -and [string]::IsNullOrWhiteSpace($ApplicationId)) {
    throw "Quest/OpenXR application ID is required. Pass --application-id or set TERMIN_ANDROID_APPLICATION_ID."
}

if ($Variant -eq "release") {
    $signingVariables = @(
        "TERMIN_ANDROID_SIGNING_KEYSTORE",
        "TERMIN_ANDROID_SIGNING_KEY_ALIAS",
        "TERMIN_ANDROID_SIGNING_STORE_PASSWORD",
        "TERMIN_ANDROID_SIGNING_KEY_PASSWORD"
    )
    $missingSigning = @($signingVariables | Where-Object {
        [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($_))
    })
    if ($missingSigning.Count -gt 0) {
        throw "Android release builds require signing configuration. Missing environment variables: $($missingSigning -join ', ')"
    }
    if (-not (Test-Path -LiteralPath $env:TERMIN_ANDROID_SIGNING_KEYSTORE -PathType Leaf)) {
        throw "Android release signing keystore does not exist: $env:TERMIN_ANDROID_SIGNING_KEYSTORE"
    }
}

if (-not (Get-Command -Name $GradleBin -ErrorAction SilentlyContinue)) {
    throw "Gradle executable not found: $GradleBin. Install Gradle 8.x or pass --gradle PATH."
}

$gradleVersionOutput = @(& $GradleBin --version)
if ($LASTEXITCODE -ne 0) {
    throw "Gradle version probe failed with exit code $LASTEXITCODE."
}
$gradleVersionLine = $gradleVersionOutput | Where-Object { $_ -match "^Gradle\s+(.+)$" } | Select-Object -First 1
if (-not $gradleVersionLine) {
    throw "Cannot determine Gradle version from: $GradleBin"
}
$gradleVersion = ([regex]::Match($gradleVersionLine, "^Gradle\s+(.+)$")).Groups[1].Value
$gradleMajor = 0
if (-not [int]::TryParse(($gradleVersion -split "\.")[0], [ref]$gradleMajor) -or $gradleMajor -lt 8) {
    throw "Gradle 8.x is required, found: $gradleVersion."
}

if ($IsQuest) {
    $sdkPrefix = Join-Path $AndroidSdkRoot $Abi
    $sdkLibDir = Join-Path $sdkPrefix "lib"
    $openXrConfig = Join-Path $sdkLibDir "cmake\termin_openxr\termin_openxrConfig.cmake"
    if (-not (Test-Path -LiteralPath $AndroidSdkRoot -PathType Container)) {
        throw "Termin Android SDK is not installed: $AndroidSdkRoot"
    }
    if (-not (Test-Path -LiteralPath $sdkPrefix -PathType Container)) {
        throw "Termin Android SDK is missing ABI '$Abi': $sdkPrefix"
    }
    if (-not (Test-Path -LiteralPath $sdkLibDir -PathType Container)) {
        throw "Termin Android SDK ABI prefix is incomplete: $sdkLibDir"
    }
    if (-not (Test-Path -LiteralPath $openXrConfig -PathType Leaf)) {
        throw "Termin Android SDK is missing OpenXR support: $openXrConfig"
    }
}

if ($InstallApk -and -not (Get-Command -Name $AdbBin -ErrorAction SilentlyContinue)) {
    throw "adb executable not found: $AdbBin. Pass --adb PATH or set ADB."
}

if (-not $env:GRADLE_USER_HOME) {
    $env:GRADLE_USER_HOME = Join-Path $TerminRoot "build\gradle-home"
}
$projectCacheDir = Join-Path $GradleBuildRoot "project-cache"
$apkOutputDir = Join-Path $GradleBuildRoot "app\outputs\apk\$Variant"

if (-not $IsQuest) {
    if (-not (Test-Path -LiteralPath $HostPython -PathType Leaf)) {
        throw "Termin host Python was not found: $HostPython. Run build-sdk.ps1 first or set TERMIN_HOST_PYTHON."
    }
    $stagedAssetsDir = Join-Path $GradleBuildRoot "runtime-assets"
    & $HostPython -m termin.project_build.android_runtime_assets `
        --source $AssetsDir `
        --output $stagedAssetsDir `
        --shader-compiler $ShaderCompiler
    if ($LASTEXITCODE -ne 0) {
        throw "Android runtime asset preparation failed with exit code $LASTEXITCODE."
    }
    if (-not (Test-Path -LiteralPath $stagedAssetsDir -PathType Container)) {
        throw "Android runtime asset preparation did not create: $stagedAssetsDir"
    }
    $AssetsDir = $stagedAssetsDir
}

Write-Output ""
Write-Output "========================================"
Write-Output $(if ($IsQuest) { "  Building Termin Quest/OpenXR smoke APK" } else { "  Building Termin Android APK" })
Write-Output "========================================"
Write-Output ""
Write-Output "Gradle:          $GradleBin ($gradleVersion)"
Write-Output "Gradle home:     $env:GRADLE_USER_HOME"
Write-Output "Project cache:   $projectCacheDir"
Write-Output "Project:         $PlatformDir"
Write-Output "Task:            $GradleTask"
Write-Output "Variant:         $Variant"
Write-Output "Termin SDK root: $AndroidSdkRoot"
Write-Output "Android SDK:      $SystemAndroidSdkRoot"
Write-Output "Android NDK:      $(if ($AndroidNdkRoot) { $AndroidNdkRoot } else { '<SDK-managed version>' })"
Write-Output "Java home:        $(if ($JavaHome) { $JavaHome } else { '<Gradle/default>' })"
Write-Output "ABI:             $Abi"
Write-Output "Platform:        $Platform"
Write-Output "NDK version:     $NdkVersion"
Write-Output $(if ($IsQuest) { "Assets dir:      $AssetsDir" } else { "Prepared assets: $AssetsDir" })
Write-Output "Application ID:  $ApplicationId"
Write-Output "App label:       $AppLabel"
Write-Output "Version:         $VersionName ($VersionCode)"
Write-Output ""

$gradleArguments = @(
    "--no-daemon",
    $GradleTask,
    "--project-cache-dir",
    $projectCacheDir,
    "-PterminAndroidSdkRoot=$AndroidSdkRoot",
    "-PterminAndroidAbi=$Abi",
    "-PterminAndroidPlatform=$Platform",
    "-PterminAndroidNdkVersion=$NdkVersion"
)
if (-not [string]::IsNullOrWhiteSpace($AndroidNdkRoot)) {
    $gradleArguments += "-PterminAndroidNdkRoot=$AndroidNdkRoot"
}
if ($IsQuest) {
    $gradleArguments += "-PterminOpenXRAssetsDir=$AssetsDir"
} else {
    $gradleArguments += "-PterminAndroidAssetsDir=$AssetsDir"
}
$gradleArguments += @(
    "-PterminAndroidApplicationId=$ApplicationId",
    "-PterminAndroidAppLabel=$AppLabel",
    "-PterminAndroidVersionCode=$VersionCode",
    "-PterminAndroidVersionName=$VersionName"
)

Push-Location $PlatformDir
try {
    & $GradleBin @gradleArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Gradle APK build failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}

$resolvedPlatformDir = [IO.Path]::GetFullPath($PlatformDir).TrimEnd("\", "/")
foreach ($relativePath in @(".gradle", "app\.cxx", "app\build")) {
    $generatedPath = [IO.Path]::GetFullPath((Join-Path $resolvedPlatformDir $relativePath))
    $expectedPrefix = $resolvedPlatformDir + [IO.Path]::DirectorySeparatorChar
    if (-not $generatedPath.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean generated path outside the platform directory: $generatedPath"
    }
    if (Test-Path -LiteralPath $generatedPath) {
        Remove-Item -LiteralPath $generatedPath -Recurse -Force
    }
}

$metadataPath = Join-Path $apkOutputDir "output-metadata.json"
Write-Output ""
Write-Output "Gradle APK metadata: $metadataPath"

if ($InstallApk) {
    if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
        throw "Gradle artifact metadata does not exist: $metadataPath"
    }
    $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
    if ($metadata.elements.Count -ne 1 -or [string]::IsNullOrWhiteSpace($metadata.elements[0].outputFile)) {
        throw "Expected exactly one APK in Gradle artifact metadata: $metadataPath"
    }
    $apkPath = Join-Path $apkOutputDir $metadata.elements[0].outputFile
    if (-not (Test-Path -LiteralPath $apkPath -PathType Leaf)) {
        throw "Gradle artifact metadata points to a missing APK: $apkPath"
    }
    & $AdbBin install -r $apkPath
    if ($LASTEXITCODE -ne 0) {
        throw "adb install failed with exit code $LASTEXITCODE."
    }
}

if ($LaunchOpenXR) {
    & $AdbBin shell input keyevent KEYCODE_WAKEUP
    if ($LASTEXITCODE -ne 0) {
        throw "adb wakeup failed with exit code $LASTEXITCODE."
    }
    & $AdbBin shell monkey -p $ApplicationId 1
    if ($LASTEXITCODE -ne 0) {
        throw "adb launch failed with exit code $LASTEXITCODE."
    }
}
