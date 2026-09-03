[CmdletBinding()]
param(
    [string]$CloudMusicPath,
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'NCM Mini'),
    [switch]$NoStart
)

$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
if (-not (Test-Path (Join-Path $source 'NCMMini.exe'))) {
    $candidate = Join-Path $PSScriptRoot '..\artifacts\publish'
    if (Test-Path (Join-Path $candidate 'NCMMini.exe')) {
        $source = (Resolve-Path $candidate).Path
    }
}
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
foreach ($file in $requiredFiles) {
    $sourcePath = Join-Path $source $file
    $targetPath = Join-Path $InstallDirectory $file
    $existingTarget = Resolve-Path $targetPath -ErrorAction SilentlyContinue
    if (-not $existingTarget -or (Resolve-Path $sourcePath).Path -ne $existingTarget.Path) {
        Copy-Item $sourcePath $targetPath -Force
    }
}

$dll = Join-Path $InstallDirectory 'NCMMiniBand.dll'
$registration = Start-Process "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList @('/s', "`"$dll`"") -Wait -PassThru
if ($registration.ExitCode -ne 0) {
    throw "DeskBand registration failed with exit code $($registration.ExitCode)."
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
