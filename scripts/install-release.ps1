param(
    [Parameter(Mandatory=$true)]
    [string]$Project,
    [string]$PackageRoot
)

$ErrorActionPreference = 'Stop'

function Resolve-ExistingPath([string]$Path, [string]$Description) {
    if (!(Test-Path -LiteralPath $Path)) { throw "${Description} was not found: $Path" }
    return (Resolve-Path -LiteralPath $Path).Path
}

$projectPath = Resolve-ExistingPath $Project 'Project file'
if ([IO.Path]::GetExtension($projectPath) -ne '.uproject') { throw 'Project must be an existing .uproject file.' }

if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
    $PackageRoot = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'Plugins\UEBlueprintBridge')) {
        $PSScriptRoot
    } else {
        Split-Path $PSScriptRoot -Parent
    }
}
$packagePath = Resolve-ExistingPath $PackageRoot 'Release package'
$pluginSource = Join-Path $packagePath 'Plugins\UEBlueprintBridge'
$pluginSource = Resolve-ExistingPath $pluginSource 'Packaged UEBlueprintBridge plugin'
$manifestPath = Join-Path $pluginSource 'UEBlueprintBridge.uplugin'
$binaryPath = Join-Path $pluginSource 'Binaries\Win64\UE4Editor-UEBlueprintBridge.dll'
$modulesPath = Join-Path $pluginSource 'Binaries\Win64\UE4Editor.modules'
foreach ($required in @($manifestPath, $binaryPath, $modulesPath)) {
    Resolve-ExistingPath $required 'Release plugin file' | Out-Null
}
$releaseManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($releaseManifest.VersionName -ne '0.5.1') { throw 'Release package is not a current core build.' }
$packageManifestPath = Join-Path $packagePath 'package-manifest.json'
if (Test-Path -LiteralPath $packageManifestPath) {
    $packageManifest = Get-Content -LiteralPath $packageManifestPath -Raw | ConvertFrom-Json
    $actualHash = (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash
    if ($packageManifest.plugin_version -ne $releaseManifest.VersionName -or $packageManifest.dll_sha256 -ne $actualHash) {
        throw 'Release package manifest does not match the plugin binary.'
    }
}

$projectDir = Split-Path $projectPath -Parent
$destination = Join-Path $projectDir 'Plugins\UEBlueprintBridge'
$destinationRoot = (Resolve-Path $projectDir).Path
$backup = Join-Path $projectDir ('Saved\UEBlueprintBridge\install-backups\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $destination, $backup -Force | Out-Null

# Preserve the previous manifest and module map before replacing the package.
foreach ($relative in @('UEBlueprintBridge.uplugin', 'Binaries\Win64\UE4Editor.modules')) {
    $old = Join-Path $destination $relative
    if (Test-Path -LiteralPath $old) {
        Copy-Item -LiteralPath $old -Destination (Join-Path $backup ([IO.Path]::GetFileName($old))) -Force
    }
}

$binaryDir = Join-Path $destination 'Binaries\Win64'
New-Item -ItemType Directory -Path $binaryDir -Force | Out-Null
$suffix = (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash.Substring(0,12).ToLowerInvariant()
$dllName = "UE4Editor-UEBlueprintBridge-$suffix.dll"
Copy-Item -LiteralPath $binaryPath -Destination (Join-Path $binaryDir $dllName) -Force

$manifest = Get-Content -LiteralPath $modulesPath -Raw | ConvertFrom-Json
$manifest.Modules.UEBlueprintBridge = $dllName
$tempManifest = Join-Path $binaryDir 'UE4Editor.modules.tmp'
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $tempManifest -Encoding ascii
Move-Item -LiteralPath $tempManifest -Destination (Join-Path $binaryDir 'UE4Editor.modules') -Force

Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $destination 'UEBlueprintBridge.uplugin') -Force
foreach ($folder in @('Config', 'Content', 'Resources', 'Source')) {
    $sourceFolder = Join-Path $pluginSource $folder
    if (Test-Path -LiteralPath $sourceFolder) {
        Copy-Item -LiteralPath $sourceFolder -Destination $destination -Recurse -Force
    }
}
# Remove source files retired by the current core profile when upgrading an older install.
$obsoleteSource = Join-Path $destination 'Source\UEBlueprintBridge\Private\BlendSpaceRebuild.cpp'
if (Test-Path -LiteralPath $obsoleteSource) { Remove-Item -LiteralPath $obsoleteSource -Force }

$installedVersion = (Get-Content -LiteralPath (Join-Path $destination 'UEBlueprintBridge.uplugin') -Raw | ConvertFrom-Json).VersionName
Write-Output "Installed UEBlueprintBridge ${installedVersion}: $destination"
Write-Output "DLL: $dllName"
Write-Output "Backup: $backup"
Write-Output 'Restart the Unreal Editor to load the plugin.'
