[CmdletBinding()]
param(
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'NCM Mini')
)

$ErrorActionPreference = 'Stop'
Get-Process NCMMini -ErrorAction SilentlyContinue | Stop-Process -Force

$controller = Join-Path $InstallDirectory 'NCMMiniBandCtl.exe'
if (Test-Path $controller) {
    & $controller hide | Out-Null
}

$dll = Join-Path $InstallDirectory 'NCMMiniBand.dll'
if (Test-Path $dll) {
    Start-Process "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList @('/u', '/s', "`"$dll`"") -Wait | Out-Null
}

$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'NCM Mini.lnk'
Remove-Item $shortcut -Force -ErrorAction SilentlyContinue
Remove-Item $InstallDirectory -Recurse -Force -ErrorAction SilentlyContinue
Write-Host 'NCM Mini was uninstalled for the current user.'
