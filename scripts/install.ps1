[CmdletBinding()]
param(
    [string]$CloudMusicPath,
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'NCM Mini'),
    [switch]$NoStart
)

$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
$requiredFiles = @('NCMMini.exe', 'NCMMiniBand.dll', 'NCMMiniBandCtl.exe', 'uninstall.ps1')
foreach ($file in $requiredFiles) {
    if (-not (Test-Path (Join-Path $source $file))) {
        throw "Missing build artifact: $file. Run scripts\build.ps1 first."
    }
}

Get-Process NCMMini -ErrorAction SilentlyContinue | Stop-Process -Force
$oldController = Join-Path $InstallDirectory 'NCMMiniBandCtl.exe'
if (Test-Path $oldController) {
    & $oldController hide | Out-Null
}

New-Item $InstallDirectory -ItemType Directory -Force | Out-Null
foreach ($file in $requiredFiles + 'install.ps1') {
    $sourcePath = Join-Path $source $file
    $targetPath = Join-Path $InstallDirectory $file
    if ((Resolve-Path $sourcePath).Path -ne (Resolve-Path $targetPath -ErrorAction SilentlyContinue).Path) {
        Copy-Item $sourcePath $targetPath -Force
    }
}

$dll = Join-Path $InstallDirectory 'NCMMiniBand.dll'
& "$env:SystemRoot\System32\regsvr32.exe" /s $dll
if ($LASTEXITCODE -ne 0) {
    throw "DeskBand registration failed with exit code $LASTEXITCODE."
}

$controller = Join-Path $InstallDirectory 'NCMMiniBandCtl.exe'
& $controller refresh | Out-Null

$programs = [Environment]::GetFolderPath('Programs')
$shortcutPath = Join-Path $programs 'NCM Mini.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = Join-Path $InstallDirectory 'NCMMini.exe'
$shortcut.WorkingDirectory = $InstallDirectory
if ($CloudMusicPath) {
    $shortcut.Arguments = "--cloudmusic `"$CloudMusicPath`""
}
$shortcut.Save()

if (-not $NoStart) {
    $arguments = @()
    if ($CloudMusicPath) {
        $arguments = @('--cloudmusic', $CloudMusicPath)
    }
    Start-Process (Join-Path $InstallDirectory 'NCMMini.exe') -ArgumentList $arguments
}

Write-Host "NCM Mini installed for the current user: $InstallDirectory"

