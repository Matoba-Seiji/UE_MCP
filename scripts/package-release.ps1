param(
    [Parameter(Mandatory=$true)]
    [string]$BuiltPlugin,
    [string]$OutputRoot = '',
    [string]$EngineVersion = '4.24',
    [string]$Platform = 'Win64'
)

$ErrorActionPreference = 'Stop'

function Resolve-ExistingPath([string]$Path, [string]$Description) {
    if (!(Test-Path -LiteralPath $Path)) { throw "${Description} was not found: $Path" }
    return (Resolve-Path -LiteralPath $Path).Path
}

$workspace = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$pluginPath = Resolve-ExistingPath $BuiltPlugin 'Built plugin directory'
$manifestPath = Join-Path $pluginPath 'UEBlueprintBridge.uplugin'
$binaryDir = Join-Path $pluginPath 'Binaries\Win64'
$dllPath = Join-Path $binaryDir 'UE4Editor-UEBlueprintBridge.dll'
$modulesPath = Join-Path $binaryDir 'UE4Editor.modules'
foreach ($required in @($manifestPath, $dllPath, $modulesPath)) {
    Resolve-ExistingPath $required 'Built plugin file' | Out-Null
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.VersionName -ne '0.5.1') {
    throw "Expected core VersionName 0.5.1, found $($manifest.VersionName). Refusing to package stale or incompatible binaries."
}
if ($manifest.EngineVersion -and $manifest.EngineVersion -notlike "$EngineVersion*") {
    throw "Built plugin targets EngineVersion $($manifest.EngineVersion), expected $EngineVersion."
}

$moduleManifest = Get-Content -LiteralPath $modulesPath -Raw | ConvertFrom-Json
if (!$moduleManifest.Modules.UEBlueprintBridge) { throw 'UEBlueprintBridge is missing from UE4Editor.modules.' }

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $workspace 'releases'
}
$outputBase = (Resolve-Path -LiteralPath (New-Item -ItemType Directory -Path $OutputRoot -Force)).Path
$packageName = "UEBlueprintBridge-$($manifest.VersionName)-UE$EngineVersion-$Platform"
$packageRoot = Join-Path $outputBase $packageName
if (Test-Path -LiteralPath $packageRoot) {
    $resolvedPackage = (Resolve-Path -LiteralPath $packageRoot).Path
    $resolvedOutput = (Resolve-Path -LiteralPath $outputBase).Path
    if (!$resolvedPackage.StartsWith($resolvedOutput + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace package outside releases output: $resolvedPackage"
    }
    Remove-Item -LiteralPath $resolvedPackage -Recurse -Force
}
$pluginDestination = Join-Path $packageRoot 'Plugins\UEBlueprintBridge'
New-Item -ItemType Directory -Path $pluginDestination -Force | Out-Null

Copy-Item -LiteralPath $manifestPath -Destination $pluginDestination -Force
foreach ($folder in @('Config', 'Source')) {
    $sourceFolder = Join-Path $pluginPath $folder
    if (Test-Path -LiteralPath $sourceFolder) {
        Copy-Item -LiteralPath $sourceFolder -Destination $pluginDestination -Recurse -Force
    }
}
$packageBinaryDir = Join-Path $pluginDestination 'Binaries\Win64'
New-Item -ItemType Directory -Path $packageBinaryDir -Force | Out-Null
# Publish only runtime plugin files. PDBs and intermediate artifacts stay local.
Copy-Item -LiteralPath $dllPath -Destination $packageBinaryDir -Force
$packagedModulesPath = Join-Path $packageBinaryDir 'UE4Editor.modules'
$normalizedModules = (Get-Content -LiteralPath $modulesPath) | ForEach-Object { $_.TrimEnd() }
Set-Content -LiteralPath $packagedModulesPath -Value $normalizedModules -Encoding ascii

$hash = (Get-FileHash -LiteralPath (Join-Path $pluginDestination 'Binaries\Win64\UE4Editor-UEBlueprintBridge.dll') -Algorithm SHA256).Hash
$metadata = [ordered]@{
    package = $packageName
    plugin_version = $manifest.VersionName
    engine_version = $EngineVersion
    platform = $Platform
    dll = 'Plugins/UEBlueprintBridge/Binaries/Win64/UE4Editor-UEBlueprintBridge.dll'
    dll_sha256 = $hash
    includes_debug_symbols = $false
    packaged_at_utc = (Get-Date).ToUniversalTime().ToString('o')
}
$metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $packageRoot 'package-manifest.json') -Encoding ascii

Write-Output "Packaged core plugin: $packageRoot"
Write-Output "DLL SHA-256: $hash"
