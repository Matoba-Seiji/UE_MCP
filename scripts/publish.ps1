param(
    [Parameter(Mandatory=$true)]
    [string]$BuiltPlugin,
    [Parameter(Mandatory=$true)]
    [string]$Message,
    [string]$EngineVersion = '4.24',
    [string]$Platform = 'Win64',
    [string]$Remote = 'origin',
    [string]$Branch = 'main'
)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
& (Join-Path $PSScriptRoot 'package-release.ps1') -BuiltPlugin $BuiltPlugin -EngineVersion $EngineVersion -Platform $Platform
if ($LASTEXITCODE -ne 0) { throw "Package step failed: $LASTEXITCODE" }

Push-Location $workspace
try {
    git add --all
    if ($LASTEXITCODE -ne 0) { throw "git add failed: $LASTEXITCODE" }
    git diff --cached --check
    if ($LASTEXITCODE -ne 0) { throw 'Staged diff has whitespace errors.' }
    git commit -m $Message
    if ($LASTEXITCODE -ne 0) { throw "git commit failed: $LASTEXITCODE" }
    git push $Remote "HEAD:$Branch"
    if ($LASTEXITCODE -ne 0) { throw "git push failed: $LASTEXITCODE" }
}
finally {
    Pop-Location
}
