[CmdletBinding()]
param(
    [string]$CloudMusicPath = 'D:\Apps\Netease\CloudMusic\cloudmusic.exe'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$publish = Join-Path $root 'artifacts\publish'
if (-not (Test-Path (Join-Path $publish 'NCMMini.exe'))) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Debug -FrameworkDependent
}

& "$env:SystemRoot\System32\regsvr32.exe" /s (Join-Path $publish 'NCMMiniBand.dll')
& (Join-Path $publish 'NCMMiniBandCtl.exe') refresh | Out-Null
Start-Process (Join-Path $publish 'NCMMini.exe') -ArgumentList @('--cloudmusic', $CloudMusicPath)
