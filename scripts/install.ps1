[CmdletBinding()]
param(
    [string]$CloudMusicPath,
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'NCM Mini'),
    [switch]$NoStart,
    [switch]$NoExplorerRestart
)

$ErrorActionPreference = 'Stop'

function Find-CloudMusicPath {
    $candidates = [System.Collections.Generic.List[string]]::new()

    Get-Process cloudmusic -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            if ($_.Path) {
                [void]$candidates.Add($_.Path)
            } elseif ($_.MainModule.FileName) {
                [void]$candidates.Add($_.MainModule.FileName)
            }
        } catch {
        }
    }

    [void]$candidates.Add('D:\Apps\Netease\CloudMusic\cloudmusic.exe')
    [void]$candidates.Add((Join-Path $env:LOCALAPPDATA 'NetEase\CloudMusic\cloudmusic.exe'))
    [void]$candidates.Add((Join-Path $env:ProgramFiles 'NetEase\CloudMusic\cloudmusic.exe'))
    if (${env:ProgramFiles(x86)}) {
        [void]$candidates.Add((Join-Path ${env:ProgramFiles(x86)} 'NetEase\CloudMusic\cloudmusic.exe'))
    }

    $shortcutRoots = @(
        (Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs'),
        (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs')
    ) | Where-Object { Test-Path $_ }
    if ($shortcutRoots) {
        $shell = New-Object -ComObject WScript.Shell
        Get-ChildItem $shortcutRoots -Filter '*.lnk' -File -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
            try {
                $shortcut = $shell.CreateShortcut($_.FullName)
                if ($shortcut.TargetPath -and [System.IO.Path]::GetFileName($shortcut.TargetPath) -ieq 'cloudmusic.exe') {
                    [void]$candidates.Add($shortcut.TargetPath)
                }
            } catch {
            }
        }
    }

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate)) {
            try {
                $fullPath = [System.IO.Path]::GetFullPath($candidate)
                if (([System.IO.Path]::GetFileName($fullPath) -ieq 'cloudmusic.exe') -and (Test-Path $fullPath -PathType Leaf)) {
                    return $fullPath
                }
            } catch {
            }
        }
    }
    return $null
}

if ([string]::IsNullOrWhiteSpace($CloudMusicPath)) {
    $CloudMusicPath = Find-CloudMusicPath
    if ($CloudMusicPath) {
        Write-Host "Detected CloudMusic: $CloudMusicPath"
    }
}

$cloudMusicIsValid = -not [string]::IsNullOrWhiteSpace($CloudMusicPath) `
    -and ([System.IO.Path]::GetFileName($CloudMusicPath) -ieq 'cloudmusic.exe') `
    -and (Test-Path $CloudMusicPath -PathType Leaf)
if (-not $cloudMusicIsValid) {
    $message = "未找到网易云音乐（cloudmusic.exe）。`r`n`r`n请先安装网易云音乐，或在 PowerShell 中使用 -CloudMusicPath 指定其完整路径。"
    try {
        $shell = New-Object -ComObject WScript.Shell
        [void]$shell.Popup($message, 0, 'NCM Mini 安装', 0x10)
    } catch {
        Write-Warning $message
    }
    throw $message
}
$CloudMusicPath = (Resolve-Path $CloudMusicPath).Path

$source = $PSScriptRoot
if (-not (Test-Path (Join-Path $source 'NCMMini.exe'))) {
    $candidate = Join-Path $PSScriptRoot '..\artifacts\publish'
    if (Test-Path (Join-Path $candidate 'NCMMini.exe')) {
        $source = (Resolve-Path $candidate).Path
    }
}
$requiredFiles = @('NCMMini.exe', 'NCMMiniBand.dll', 'NCMMiniBandCtl.exe', 'uninstall.ps1', 'config.ini')
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
$oldDll = Join-Path $InstallDirectory 'NCMMiniBand.dll'
if (Test-Path $oldDll) {
    Start-Process "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList @('/u', '/s', "`"$oldDll`"") -Wait | Out-Null
}

New-Item $InstallDirectory -ItemType Directory -Force | Out-Null
try {
    foreach ($file in $requiredFiles) {
        $sourcePath = Join-Path $source $file
        $targetPath = Join-Path $InstallDirectory $file
        if ($file -eq 'config.ini' -and (Test-Path $targetPath)) {
            continue
        }
        $existingTarget = Resolve-Path $targetPath -ErrorAction SilentlyContinue
        if (-not $existingTarget -or (Resolve-Path $sourcePath).Path -ne $existingTarget.Path) {
            Copy-Item $sourcePath $targetPath -Force -ErrorAction Stop
        }
    }
} catch {
    Write-Host 'Restarting Explorer to replace the previous DeskBand...'
    Get-Process explorer -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 1
    foreach ($file in $requiredFiles) {
        if ($file -eq 'config.ini' -and (Test-Path (Join-Path $InstallDirectory $file))) {
            continue
        }
        Copy-Item (Join-Path $source $file) (Join-Path $InstallDirectory $file) -Force
    }
    Start-Process explorer.exe
    Start-Sleep -Seconds 2
}

$dll = Join-Path $InstallDirectory 'NCMMiniBand.dll'
$registration = Start-Process "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList @('/s', "`"$dll`"") -Wait -PassThru
if ($registration.ExitCode -ne 0) {
    throw "DeskBand registration failed with exit code $($registration.ExitCode)."
}

$controller = Join-Path $InstallDirectory 'NCMMiniBandCtl.exe'
& $controller refresh 2>$null | Out-Null

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

$startedHost = $null
if (-not $NoStart) {
    $hostArguments = "--cloudmusic `"$CloudMusicPath`" --no-show-band"
    $startedHost = Start-Process (Join-Path $InstallDirectory 'NCMMini.exe') -ArgumentList $hostArguments -PassThru
    Start-Sleep -Milliseconds 500
}

function Test-DeskBandShown {
    & $controller status 2>$null | Out-Null
    return $LASTEXITCODE -eq 0
}

function Wait-DeskBandShown {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        if (Test-DeskBandShown) {
            return $true
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Show-DeskBand {
    & $controller show 2>$null | Out-Null
    return (Wait-DeskBandShown)
}

$deskBandShown = Test-DeskBandShown
if (-not $deskBandShown) {
    $deskBandShown = Show-DeskBand
}
if (-not $deskBandShown -and -not $NoExplorerRestart) {
    Write-Host 'Restarting Explorer once to load the new DeskBand...'
    Get-Process explorer -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 1
    Start-Process explorer.exe
    Start-Sleep -Seconds 3
    & $controller refresh 2>$null | Out-Null
    $deskBandShown = Show-DeskBand
}
if (-not $deskBandShown) {
    if ($startedHost -and -not $startedHost.HasExited) {
        $startedHost | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    $message = if ($NoExplorerRestart) {
        'NCM Mini 已注册，但需要重启 Windows 资源管理器后才能添加到任务栏。'
    } else {
        '未能把 NCM Mini 添加到任务栏。请在弹出的确认窗口中选择“是”，然后重新运行安装程序。'
    }
    try {
        $shell = New-Object -ComObject WScript.Shell
        [void]$shell.Popup($message, 0, 'NCM Mini 安装', 0x1030)
    } catch {
        Write-Warning $message
    }
    throw $message
}

Write-Host "NCM Mini installed for the current user: $InstallDirectory"
