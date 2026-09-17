param(
    [Parameter(Mandatory=$true)]
    [string]$Project
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Project -PathType Leaf) -or [IO.Path]::GetExtension($Project) -ne '.uproject') { throw 'Expected an existing .uproject.' }
$workspace = Split-Path $PSScriptRoot -Parent
$built = Join-Path $workspace 'build\Verify\Plugins\UEBlueprintBridge\Binaries\Win64'
$dll = Join-Path $built 'UE4Editor-UEBlueprintBridge.dll'
if (!(Test-Path -LiteralPath $dll)) { throw 'Build the plugin first.' }
$profileMarker = Join-Path $built 'core-build.json'
if (!(Test-Path -LiteralPath $profileMarker)) { throw 'Build the current core plugin first; stale binaries are not accepted.' }
$profile = Get-Content -LiteralPath $profileMarker -Raw | ConvertFrom-Json
if ($profile.profile -ne 'core' -or $profile.plugin_version -ne '0.5.1') { throw 'The staged plugin is not a current core build.' }
$projectDir = Split-Path (Resolve-Path -LiteralPath $Project).Path -Parent
$destination = Join-Path $projectDir 'Plugins\UEBlueprintBridge'
$binaryDir = Join-Path $destination 'Binaries\Win64'
$backup = Join-Path $projectDir ('Saved\UEBlueprintBridge\install-backups\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $binaryDir,$backup -Force | Out-Null
foreach ($relative in @('UEBlueprintBridge.uplugin','Binaries\Win64\UE4Editor.modules')) {
    $old = Join-Path $destination $relative
    if (Test-Path -LiteralPath $old) { Copy-Item -LiteralPath $old -Destination (Join-Path $backup ([IO.Path]::GetFileName($old))) }
}
# A unique filename avoids replacing a DLL loaded by the user's current editor.
$suffix = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.Substring(0,12).ToLowerInvariant()
$dllName = "UE4Editor-UEBlueprintBridge-$suffix.dll"
$newDll = Join-Path $binaryDir $dllName
if (!(Test-Path -LiteralPath $newDll)) { Copy-Item -LiteralPath $dll -Destination $newDll }
$manifest = Get-Content -LiteralPath (Join-Path $built 'UE4Editor.modules') -Raw | ConvertFrom-Json
$manifest.Modules.UEBlueprintBridge = $dllName
Copy-Item -LiteralPath "$workspace\Plugins\UEBlueprintBridge\Source" -Destination $destination -Recurse -Force
Copy-Item -LiteralPath "$workspace\Plugins\UEBlueprintBridge\UEBlueprintBridge.uplugin" -Destination $destination -Force
# Remove source files retired by the current core profile when upgrading an older install.
$obsoleteSource = Join-Path $destination 'Source\UEBlueprintBridge\Private\BlendSpaceRebuild.cpp'
if (Test-Path -LiteralPath $obsoleteSource) { Remove-Item -LiteralPath $obsoleteSource -Force }
$tempManifest = Join-Path $binaryDir 'UE4Editor.modules.tmp'
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $tempManifest -Encoding ascii
Move-Item -LiteralPath $tempManifest -Destination (Join-Path $binaryDir 'UE4Editor.modules') -Force
$installedVersion = (Get-Content -LiteralPath (Join-Path $destination 'UEBlueprintBridge.uplugin') -Raw | ConvertFrom-Json).VersionName
Write-Output "Installed ${installedVersion}: $destination"
Write-Output "DLL: $dllName"
Write-Output "Restart this project's editor to load the update. Previous manifest backup: $backup"
