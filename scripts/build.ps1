param(
    [Parameter(Mandatory=$true)]
    [string]$Engine,
    # This workspace uses the installed VS2019 toolchain.
    [string]$CompilerVersion = '14.29.30133',
    [string]$WindowsSdkVersion = '10.0.19041.0'
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Engine -PathType Container)) { throw 'Expected -Engine to point to a UE4.24 installation.' }
if ($CompilerVersion -notmatch '^\d+\.\d+\.\d+$' -or $WindowsSdkVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') {
    throw 'Invalid compiler or SDK version.'
}
$workspace = Split-Path $PSScriptRoot -Parent
$hostProject = Join-Path $workspace 'build\Verify'
New-Item -ItemType Directory -Path "$hostProject\Plugins", "$hostProject\Source" -Force | Out-Null
# Verify is generated output; clear the staged plugin so removed source files
# from an earlier profile cannot be picked up by UnrealBuildTool.
$stagedPlugin = Join-Path $hostProject 'Plugins\UEBlueprintBridge'
if (Test-Path -LiteralPath $stagedPlugin) { Remove-Item -LiteralPath $stagedPlugin -Recurse -Force }
Copy-Item -LiteralPath "$workspace\Plugins\UEBlueprintBridge" -Destination "$hostProject\Plugins" -Recurse -Force
Set-Content -LiteralPath "$hostProject\Verify.uproject" -Encoding ascii -Value '{"FileVersion":3,"Plugins":[{"Name":"UEBlueprintBridge","Enabled":true}]}'
$targetRules = @"
using UnrealBuildTool;
public class VerifyEditorTarget : TargetRules
{
    public VerifyEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V2;
        WindowsPlatform.Compiler = WindowsCompiler.VisualStudio2019;
        WindowsPlatform.CompilerVersion = "$CompilerVersion";
        WindowsPlatform.WindowsSdkVersion = "$WindowsSdkVersion";
    }
}
"@
Set-Content -LiteralPath "$hostProject\Source\VerifyEditor.Target.cs" -Encoding ascii -Value $targetRules
& "$Engine\Engine\Binaries\DotNET\UnrealBuildTool.exe" VerifyEditor Win64 Development "-Project=$hostProject\Verify.uproject" -NoHotReload -NoUBTMakefiles
if ($LASTEXITCODE -ne 0) { throw "UE build failed: $LASTEXITCODE" }
$profileMarker = Join-Path $hostProject 'Plugins\UEBlueprintBridge\Binaries\Win64\core-build.json'
@{
    profile = 'core'
    plugin_version = '0.5.1'
    engine_root = (Resolve-Path -LiteralPath $Engine).Path
    built_at_utc = (Get-Date).ToUniversalTime().ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath $profileMarker -Encoding ascii
Write-Output "Built plugin: $hostProject\Plugins\UEBlueprintBridge"
